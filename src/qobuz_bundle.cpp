#include "stdafx.h"
#include "qobuz_bundle.h"

#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <cstring>

// ---- helpers ----------------------------------------------------------------

// Fetch a URL with a plain GET; returns body as a string.
// If max_bytes > 0, adds a Range header so the server only sends that many bytes.
static std::string simple_get(const char* url, abort_callback& abort,
                               size_t max_bytes = 0) {
    auto req = http_client::get()->create_request("GET");
    req->add_header("User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    if (max_bytes > 0) {
        // Range: bytes=0-N causes the server to send exactly those bytes and
        // then close the connection, preventing an infinite read loop on large files.
        char range_hdr[64];
        std::snprintf(range_hdr, sizeof(range_hdr), "bytes=0-%zu", max_bytes - 1);
        req->add_header("Range", range_hdr);
    }
    auto resp = req->run(url, abort);

    std::string body;
    body.reserve(max_bytes > 0 ? max_bytes : 65536);
    char buf[65536];
    for (;;) {
        size_t want = sizeof(buf);
        if (max_bytes > 0 && body.size() >= max_bytes) break;
        size_t got = resp->read(buf, want, abort);
        if (!got) break;
        body.append(buf, got);
    }
    return body;
}

// Extract a substring of [a-zA-Z0-9+/=] starting right after `prefix` in `s`.
static std::string extract_word_after(const std::string& s, const std::string& prefix,
                                       size_t start = 0) {
    auto pos = s.find(prefix, start);
    if (pos == std::string::npos) return {};
    pos += prefix.size();
    auto end = s.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789+/=", pos);
    if (end == std::string::npos) end = s.size();
    return s.substr(pos, end - pos);
}

// Standard base64 decode (RFC 4648 alphabet: A-Z a-z 0-9 + /).
static std::string base64_decode(const std::string& encoded) {
    static const int8_t table[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 0-15
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 16-31
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63, // 32-47  (+,/)
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1, // 48-63  (0-9)
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14, // 64-79  (A-O)
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1, // 80-95  (P-Z)
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40, // 96-111 (a-o)
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1, // 112-127(p-z)
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 128-143
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 144-159
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 160-175
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 176-191
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 192-207
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 208-223
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 224-239
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 240-255
    };
    std::string out;
    int val = 0, bits = -8;
    for (unsigned char c : encoded) {
        if (c == '=') break;
        if (table[c] == -1) continue;
        val = (val << 6) | table[c];
        bits += 6;
        if (bits >= 0) {
            out += static_cast<char>((val >> bits) & 0xFF);
            bits -= 8;
        }
    }
    return out;
}

// ---- main fetch logic -------------------------------------------------------

BundleCredentials fetch_bundle_credentials(abort_callback& abort) {
    BundleCredentials result;

    // ---- Step 1: fetch the login page to get the bundle.js URL --------------
    const char* login_url = "https://play.qobuz.com/login";
    std::string login_page = simple_get(login_url, abort);

    // Find: <script src="/resources/X.Y.Z-aXXX/bundle.js"></script>
    const char* bundle_tag = "<script src=\"/resources/";
    auto pos = login_page.find(bundle_tag);
    if (pos == std::string::npos)
        throw std::runtime_error("fetch_bundle_credentials: cannot find bundle script tag");
    pos += strlen("<script src=\"");
    auto end_quote = login_page.find('"', pos);
    if (end_quote == std::string::npos)
        throw std::runtime_error("fetch_bundle_credentials: malformed bundle script tag");
    std::string bundle_path = login_page.substr(pos, end_quote - pos);

    std::string bundle_url = "https://play.qobuz.com" + bundle_path;

    // ---- Step 2: fetch first 3 MB of bundle.js (all credentials appear within 2.2 MB) ---
    const size_t BUNDLE_LIMIT = 3 * 1024 * 1024;
    std::string bundle = simple_get(bundle_url.c_str(), abort, BUNDLE_LIMIT);
    if (bundle.size() < 100000)
        throw std::runtime_error("fetch_bundle_credentials: bundle too small, unexpected response");

    // ---- Step 3: extract app_id ---------------------------------------------
    // Pattern: production:{api:{appId:"<9-digit-id>",appSecret:"<32-char-hex>"
    const char* appid_marker = "production:{api:{appId:\"";
    auto ap = bundle.find(appid_marker);
    if (ap == std::string::npos)
        throw std::runtime_error("fetch_bundle_credentials: cannot find appId in bundle");
    ap += strlen(appid_marker);
    auto appid_end = bundle.find('"', ap);
    if (appid_end == std::string::npos)
        throw std::runtime_error("fetch_bundle_credentials: malformed appId");
    result.app_id = bundle.substr(ap, appid_end - ap);

    // ---- Step 4: extract seeds (timezone → seed) ----------------------------
    // Pattern: X.initialSeed("<seed>",window.utimezone.<timezone>)
    std::map<std::string, std::string> seed_map;
    std::vector<std::string> timezones;  // insertion order matters

    const char* seed_marker = ".initialSeed(\"";
    size_t search = 0;
    while (true) {
        auto sp = bundle.find(seed_marker, search);
        if (sp == std::string::npos) break;
        sp += strlen(seed_marker);
        auto seed_end = bundle.find('"', sp);
        if (seed_end == std::string::npos) break;
        std::string seed = bundle.substr(sp, seed_end - sp);

        // expect: ",window.utimezone.<tz>)
        const char* tz_prefix = ",window.utimezone.";
        auto tzp = bundle.find(tz_prefix, seed_end);
        if (tzp == std::string::npos || tzp - seed_end > 5) {
            search = seed_end + 1;
            continue;
        }
        tzp += strlen(tz_prefix);
        auto tz_end = bundle.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyz", tzp);
        std::string tz = bundle.substr(tzp, tz_end - tzp);

        if (!tz.empty() && seed_map.find(tz) == seed_map.end()) {
            seed_map[tz] = seed;
            timezones.push_back(tz);
        }
        search = tz_end;
    }

    if (timezones.size() < 2)
        throw std::runtime_error("fetch_bundle_credentials: not enough seeds found in bundle");

    // Python: secrets.move_to_end(keypairs[1][0], last=False)
    // → move timezones[1] to front
    {
        std::vector<std::string> reordered;
        reordered.push_back(timezones[1]);
        reordered.push_back(timezones[0]);
        for (size_t i = 2; i < timezones.size(); ++i)
            reordered.push_back(timezones[i]);
        timezones = reordered;
    }

    // ---- Step 5: for each timezone, find info + extras, compute secret ------
    // Pattern: name:"\w+/<Timezone>",info:"<info>",extras:"<extras>"
    for (auto& tz : timezones) {
        if (seed_map.find(tz) == seed_map.end()) continue;

        // Capitalize first letter for the pattern match
        std::string tz_cap = tz;
        tz_cap[0] = static_cast<char>(toupper((unsigned char)tz_cap[0]));

        // Search for: /<Timezone>",info:"<info>",extras:"<extras>"
        std::string ie_marker = "/" + tz_cap + "\",info:\"";
        auto ie_pos = bundle.find(ie_marker);
        if (ie_pos == std::string::npos) continue;

        ie_pos += ie_marker.size();
        auto info_end = bundle.find('"', ie_pos);
        if (info_end == std::string::npos) continue;
        std::string info = bundle.substr(ie_pos, info_end - ie_pos);

        const char* ext_prefix = "\",extras:\"";
        auto ex_pos = bundle.find(ext_prefix, info_end);
        if (ex_pos == std::string::npos || ex_pos - info_end > 5) continue;
        ex_pos += strlen(ext_prefix);
        auto extras_end = bundle.find('"', ex_pos);
        if (extras_end == std::string::npos) continue;
        std::string extras = bundle.substr(ex_pos, extras_end - ex_pos);

        // Concatenate seed + info + extras, strip last 44 chars, base64-decode
        std::string b64 = seed_map[tz] + info + extras;
        if (b64.size() <= 44) continue;
        b64.resize(b64.size() - 44);

        std::string secret = base64_decode(b64);
        if (!secret.empty())
            result.secrets.push_back(secret);
    }

    if (result.secrets.empty())
        throw std::runtime_error("fetch_bundle_credentials: could not derive any secrets from bundle");

    return result;
}

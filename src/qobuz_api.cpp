#include "stdafx.h"
#include "qobuz_api.h"
#include "qobuz_bundle.h"
#include <nlohmann/json.hpp>
#include <ctime>
#include <stdexcept>

using json = nlohmann::json;

// ---- advconfig settings --------------------------------------------------

static constexpr GUID guid_advcfg_branch =
    { 0x3d8f2a4e, 0x5b1c, 0x4f7d, { 0xa6, 0x2e, 0x8c, 0x9f, 0x3b, 0x4e, 0x7a, 0x16 } };
static constexpr GUID guid_cfg_auth_token =
    { 0x7a2b5c8e, 0x1d4f, 0x4a3b, { 0x96, 0x2e, 0x3f, 0x8b, 0x5d, 0x4c, 0x1a, 0x7e } };
static constexpr GUID guid_cfg_app_id =
    { 0x4c1e8b7d, 0x2f5a, 0x4e3c, { 0xb4, 0x71, 0x9a, 0x3c, 0x6e, 0x2b, 0x8d, 0x4f } };
static constexpr GUID guid_cfg_secret =
    { 0x6f3d2c8a, 0x4b9e, 0x4f1d, { 0xa7, 0x83, 0x2e, 0x5c, 0x9b, 0x3f, 0x4d, 0x6a } };
static constexpr GUID guid_cfg_quality =
    { 0x9b4e3f7c, 0x2a1d, 0x4c8b, { 0x86, 0x94, 0x4f, 0x2a, 0x7d, 0x5c, 0x3e, 0x1b } };

static advconfig_branch_factory g_advcfg_branch(
    "Qobuz", guid_advcfg_branch, advconfig_branch::guid_branch_tools, 0.0);

advconfig_string_factory g_cfg_auth_token(
    "User Auth Token",
    "foo_qobuz.auth_token", guid_cfg_auth_token, guid_advcfg_branch, 0, "");

// These two are optional: leave blank to auto-fetch from play.qobuz.com bundle.
advconfig_string_factory g_cfg_app_id(
    "App ID (leave blank to auto-fetch)",
    "foo_qobuz.app_id", guid_cfg_app_id, guid_advcfg_branch, 1, "");

advconfig_string_factory g_cfg_secret(
    "App Secret (leave blank to auto-fetch)",
    "foo_qobuz.secret", guid_cfg_secret, guid_advcfg_branch, 2, "");

advconfig_integer_factory g_cfg_quality(
    "Quality (5=MP3, 6=FLAC 16-bit, 7=FLAC 24-bit, 27=FLAC Hi-Res)",
    "foo_qobuz.quality", guid_cfg_quality, guid_advcfg_branch, 3,
    27, 5, 27);

advconfig_string_factory&  cfg_auth_token() { return g_cfg_auth_token; }
advconfig_string_factory&  cfg_app_id()     { return g_cfg_app_id; }
advconfig_string_factory&  cfg_secret()     { return g_cfg_secret; }
advconfig_integer_factory& cfg_quality()    { return g_cfg_quality; }

// ---- QobuzAPI ---------------------------------------------------------------

QobuzAPI g_qobuz_api;

void QobuzAPI::add_auth_headers(http_request::ptr& req) {
    pfc::string8 auth_token;
    g_cfg_auth_token.get(auth_token);
    req->add_header("X-App-Id",          m_app_id.c_str());
    req->add_header("X-User-Auth-Token", auth_token.c_str());
    req->add_header("User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
}

std::string QobuzAPI::do_get(const char* url, abort_callback& abort) {
    auto req = http_client::get()->create_request("GET");
    add_auth_headers(req);
    auto resp = req->run(url, abort);
    std::string body;
    char buf[8192];
    for (;;) {
        size_t got = resp->read(buf, sizeof(buf), abort);
        if (!got) break;
        body.append(buf, got);
    }
    return body;
}

void QobuzAPI::ensure_initialized(abort_callback& abort) {
    std::lock_guard<std::mutex> lock(m_init_mutex);
    if (m_initialized) return;

    pfc::string8 auth_token;
    g_cfg_auth_token.get(auth_token);
    if (auth_token.is_empty())
        throw std::runtime_error(
            "Qobuz auth token is not configured. "
            "Go to File > Preferences > Advanced > Tools > Qobuz.");

    // Check for manual override: if BOTH app_id and secret are set, use them.
    pfc::string8 manual_app_id, manual_secret;
    g_cfg_app_id.get(manual_app_id);
    g_cfg_secret.get(manual_secret);

    if (!manual_app_id.is_empty() && !manual_secret.is_empty()) {
        m_app_id  = manual_app_id.c_str();
        m_secrets = { std::string(manual_secret.c_str()) };
        m_initialized = true;
        return;
    }

    // Auto-fetch from the Qobuz web-player bundle.
    try {
        BundleCredentials creds = fetch_bundle_credentials(abort);
        m_app_id  = creds.app_id;
        m_secrets = creds.secrets;
        m_initialized = true;
    } catch (std::exception const& e) {
        // Bundle fetch failed: fall back to whatever advconfig has.
        if (!manual_app_id.is_empty()) m_app_id = manual_app_id.c_str();
        if (!manual_secret.is_empty()) m_secrets = { std::string(manual_secret.c_str()) };

        if (m_app_id.empty() || m_secrets.empty()) {
            throw std::runtime_error(
                std::string("Failed to fetch Qobuz credentials from bundle: ") + e.what() +
                "\nYou can set App ID and Secret manually in "
                "Advanced > Tools > Qobuz as a fallback.");
        }
        m_initialized = true;
    }
}

pfc::string8 QobuzAPI::get_track_url(const char* track_id, int format_id,
                                      abort_callback& abort) {
    ensure_initialized(abort);

    // Build the ordered list of secrets to try: cached winner first, then rest.
    std::vector<std::string> to_try;
    if (!m_secret.empty()) to_try.push_back(m_secret);
    for (auto& s : m_secrets)
        if (s != m_secret) to_try.push_back(s);

    std::string last_error = "no secrets available";

    for (auto& sec : to_try) {
        long unix_ts = (long)std::time(nullptr);

        char sig_input[2048];
        std::snprintf(sig_input, sizeof(sig_input),
            "trackgetFileUrlformat_id%dintentstreamtrack_id%s%ld%s",
            format_id, track_id, unix_ts, sec.c_str());

        auto md5res = hasher_md5::get()->process_single_string(sig_input);
        // Qobuz requires lowercase hex for request_sig; asString() returns uppercase
        pfc::string8 sig_hex = pfc::format_hexdump_lowercase(
            md5res.m_data, sizeof(md5res.m_data), "");

        pfc::string8 url;
        url << "https://www.qobuz.com/api.json/0.2/track/getFileUrl"
            << "?track_id="    << track_id
            << "&format_id="   << format_id
            << "&intent=stream"
            << "&request_ts="  << unix_ts
            << "&request_sig=" << sig_hex;

        try {
            auto body = do_get(url, abort);
            if (body.empty()) { last_error = "empty response"; continue; }

            auto j = json::parse(body);

            // Error JSON (wrong secret → 400, or rights issue → error in body)
            if (j.contains("status") && j["status"].is_string() &&
                j["status"].get<std::string>() == "error") {
                last_error = j.value("message", "API error");
                continue;   // try next secret
            }

            if (!j.contains("url")) {
                last_error = "no URL in response: " + body.substr(0, 200);
                continue;
            }

            m_secret = sec;  // cache the working secret
            return pfc::string8(j["url"].get<std::string>().c_str());

        } catch (std::exception const& e) {
            last_error = e.what();
            // network error or JSON parse failure — try next secret
        }
    }

    // All secrets exhausted — reset so the next call re-fetches from bundle.
    {
        std::lock_guard<std::mutex> lk(m_init_mutex);
        m_initialized = false;
        m_secret.clear();
    }
    throw std::runtime_error("Qobuz: could not get stream URL (" + last_error + ")");
}

// ---- helpers ----------------------------------------------------------------

static std::string jstr(const json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return {};
    return it->is_string() ? it->get<std::string>() : std::string{};
}

static std::string jstr_nested(const json& obj, const char* outer, const char* inner) {
    auto it = obj.find(outer);
    if (it == obj.end() || it->is_null() || !it->is_object()) return {};
    return jstr(*it, inner);
}

static QobuzTrack track_from_json(const json& t) {
    QobuzTrack tr;
    if (t.contains("id") && !t["id"].is_null()) {
        if      (t["id"].is_string()) tr.id = t["id"].get<std::string>();
        else if (t["id"].is_number()) tr.id = std::to_string(t["id"].get<long long>());
    }
    tr.title  = jstr(t, "title");
    tr.artist = jstr_nested(t, "performer", "name");
    if (tr.artist.empty()) tr.artist = jstr_nested(t, "artist", "name");
    tr.album    = jstr_nested(t, "album", "title");
    tr.album_id = jstr_nested(t, "album", "id");
    if (t.contains("duration")               && t["duration"].is_number())
        tr.duration = t["duration"].get<double>();
    if (t.contains("maximum_bit_depth")      && t["maximum_bit_depth"].is_number())
        tr.bit_depth = t["maximum_bit_depth"].get<int>();
    if (t.contains("maximum_sampling_rate")  && t["maximum_sampling_rate"].is_number())
        tr.sampling_rate = t["maximum_sampling_rate"].get<double>();
    return tr;
}

// ---- search -----------------------------------------------------------------

std::vector<QobuzTrack> QobuzAPI::search_tracks(const char* query, int limit,
                                                  abort_callback& abort) {
    ensure_initialized(abort);
    pfc::string8 encoded;
    pfc::urlEncode(encoded, query);

    pfc::string8 url;
    url << "https://www.qobuz.com/api.json/0.2/catalog/search"
        << "?query="  << encoded
        << "&type=tracks"
        << "&limit="  << limit
        << "&app_id=" << m_app_id.c_str()
        << "&lang=en&locale=en_US";

    auto j = json::parse(do_get(url, abort));
    std::vector<QobuzTrack> out;
    if (!j.contains("tracks") || !j["tracks"].contains("items")) return out;
    for (auto& t : j["tracks"]["items"])
        if (!t.is_null()) out.push_back(track_from_json(t));
    return out;
}

std::vector<QobuzAlbum> QobuzAPI::search_albums(const char* query, int limit,
                                                   abort_callback& abort) {
    ensure_initialized(abort);
    pfc::string8 encoded;
    pfc::urlEncode(encoded, query);

    pfc::string8 url;
    url << "https://www.qobuz.com/api.json/0.2/catalog/search"
        << "?query="  << encoded
        << "&type=albums"
        << "&limit="  << limit
        << "&app_id=" << m_app_id.c_str()
        << "&lang=en&locale=en_US";

    auto j = json::parse(do_get(url, abort));
    std::vector<QobuzAlbum> out;
    if (!j.contains("albums") || !j["albums"].contains("items")) return out;
    for (auto& a : j["albums"]["items"]) {
        if (a.is_null()) continue;
        QobuzAlbum al;
        al.id     = jstr(a, "id");
        al.title  = jstr(a, "title");
        al.artist = jstr_nested(a, "artist", "name");
        if (a.contains("tracks_count") && a["tracks_count"].is_number())
            al.tracks_count = a["tracks_count"].get<int>();
        if (a.contains("released_at") && a["released_at"].is_number())
            al.year = (int)(a["released_at"].get<long long>() / 31536000L + 1970);
        out.push_back(al);
    }
    return out;
}

std::vector<QobuzTrack> QobuzAPI::get_album_tracks(const char* album_id,
                                                     abort_callback& abort) {
    ensure_initialized(abort);

    pfc::string8 url;
    url << "https://www.qobuz.com/api.json/0.2/album/get"
        << "?album_id=" << album_id
        << "&app_id="   << m_app_id.c_str()
        << "&lang=en&locale=en_US";

    auto j = json::parse(do_get(url, abort));
    std::vector<QobuzTrack> out;
    if (!j.contains("tracks") || !j["tracks"].contains("items")) return out;

    std::string album_title  = jstr(j, "title");
    std::string album_artist = jstr_nested(j, "artist", "name");

    for (auto& t : j["tracks"]["items"]) {
        if (t.is_null()) continue;
        QobuzTrack tr = track_from_json(t);
        if (tr.album.empty())  tr.album  = album_title;
        if (tr.artist.empty()) tr.artist = album_artist;
        tr.album_id = album_id;
        out.push_back(tr);
    }
    return out;
}

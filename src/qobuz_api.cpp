#include "stdafx.h"
#include "qobuz_api.h"
#include "qobuz_bundle.h"
#include <nlohmann/json.hpp>
#include <ctime>
#include <stdexcept>
#include <winhttp.h>

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

// ---- WinHTTP helpers --------------------------------------------------------

struct ApiWinHttpHandle {
    HINTERNET h = nullptr;
    ~ApiWinHttpHandle() { if (h) WinHttpCloseHandle(h); }
    operator HINTERNET() const { return h; }
};


// Make a WinHTTP GET request and return {http_status_code, response_body}.
// Custom headers are added as a vector of {name, value} pairs.
// Query strings in the URL are preserved (lpszExtraInfo is properly handled).
static std::pair<DWORD, std::string> winhttp_api_get(
    const char* url_utf8,
    const std::vector<std::pair<std::string, std::string>>& headers,
    DWORD timeout_ms = 15000)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url_utf8, -1, nullptr, 0);
    std::wstring wurl(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url_utf8, -1, &wurl[0], wlen);

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]   = {};
    wchar_t path[4096]  = {};
    wchar_t extra[4096] = {};
    uc.lpszHostName  = host;  uc.dwHostNameLength  = 256;
    uc.lpszUrlPath   = path;  uc.dwUrlPathLength   = 4096;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 4096; // captures the ?query=... part
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
        throw std::runtime_error("winhttp_api_get: WinHttpCrackUrl failed");

    // Combine path and query string for WinHttpOpenRequest's pwszObjectName
    std::wstring full_path = std::wstring(path) + extra;

    ApiWinHttpHandle hSession, hConnect, hRequest;
    hSession.h = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession.h) throw std::runtime_error("winhttp_api_get: WinHttpOpen failed");

    WinHttpSetTimeouts(hSession.h, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    hConnect.h = WinHttpConnect(hSession.h, host, uc.nPort, 0);
    if (!hConnect.h) throw std::runtime_error("winhttp_api_get: WinHttpConnect failed");

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    hRequest.h = WinHttpOpenRequest(hConnect.h, L"GET", full_path.c_str(), nullptr,
                                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest.h) throw std::runtime_error("winhttp_api_get: WinHttpOpenRequest failed");

    // Build combined header string: "Name: Value\r\nName2: Value2\r\n"
    std::wstring hdrs;
    for (auto& [name, val] : headers) {
        int n1 = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
        int n2 = MultiByteToWideChar(CP_UTF8, 0, val.c_str(),  -1, nullptr, 0);
        std::wstring wname(n1 - 1, L'\0'), wval(n2 - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, &wname[0], n1);
        MultiByteToWideChar(CP_UTF8, 0, val.c_str(),  -1, &wval[0],  n2);
        hdrs += wname + L": " + wval + L"\r\n";
    }
    if (!hdrs.empty())
        WinHttpAddRequestHeaders(hRequest.h, hdrs.c_str(), (DWORD)hdrs.size(),
                                 WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hRequest.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        throw std::runtime_error("winhttp_api_get: WinHttpSendRequest failed");

    if (!WinHttpReceiveResponse(hRequest.h, nullptr))
        throw std::runtime_error("winhttp_api_get: WinHttpReceiveResponse failed");

    DWORD status = 0, status_len = sizeof(status);
    WinHttpQueryHeaders(hRequest.h,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &status_len, WINHTTP_NO_HEADER_INDEX);

    std::string body;
    char buf[65536];
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest.h, &avail) || avail == 0) break;
        DWORD to_read = (DWORD)std::min((size_t)avail, sizeof(buf));
        DWORD got = 0;
        if (!WinHttpReadData(hRequest.h, buf, to_read, &got) || got == 0) break;
        body.append(buf, got);
    }
    return {status, body};
}

// ---- QobuzAPI ---------------------------------------------------------------

QobuzAPI g_qobuz_api;

std::string QobuzAPI::do_get(const char* url, abort_callback& /*abort*/) {
    pfc::string8 auth_token;
    g_cfg_auth_token.get(auth_token);

    auto [status, body] = winhttp_api_get(url, {
        {"X-App-Id",          m_app_id},
        {"X-User-Auth-Token", std::string(auth_token.c_str())}
    });

    if (status < 200 || status >= 300) {
        // Credentials may be stale — reset so the next call re-fetches from bundle.
        {
            std::lock_guard<std::mutex> lk(m_init_mutex);
            m_initialized = false;
            m_secret.clear();
        }
        throw std::runtime_error(
            "HTTP " + std::to_string(status) + ": " +
            (body.size() > 300 ? body.substr(0, 300) + "..." : body));
    }
    return body;
}

std::string QobuzAPI::download_url(const char* url) {
    auto [status, body] = winhttp_api_get(url, {});
    if (status < 200 || status >= 300)
        throw std::runtime_error("download_url: HTTP " + std::to_string(status));
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

    // Read manual overrides (used as fallback only — Qobuz rotates app_id/secrets).
    pfc::string8 manual_app_id, manual_secret;
    g_cfg_app_id.get(manual_app_id);
    g_cfg_secret.get(manual_secret);

    // Always try to auto-fetch fresh credentials from the web-player bundle first.
    // Manual credentials are fallback only because Qobuz rotates app_id/secrets.
    try {
        BundleCredentials creds = fetch_bundle_credentials(abort);
        m_app_id  = creds.app_id;
        m_secrets = creds.secrets;
        m_initialized = true;
        return;
    } catch (std::exception const& e) {
        // Bundle fetch failed — fall back to manual credentials if set.
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

    pfc::string8 auth_token;
    g_cfg_auth_token.get(auth_token);
    std::string auth_token_str = auth_token.c_str();

    // Build ordered list of secrets to try: cached winner first, then rest.
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
        // Qobuz requires lowercase hex for request_sig
        pfc::string8 sig_hex = pfc::format_hexdump_lowercase(
            md5res.m_data, sizeof(md5res.m_data), "");

        std::string url =
            std::string("https://www.qobuz.com/api.json/0.2/track/getFileUrl")
            + "?track_id="    + track_id
            + "&format_id="   + std::to_string(format_id)
            + "&intent=stream"
            + "&request_ts="  + std::to_string(unix_ts)
            + "&request_sig=" + sig_hex.c_str();

        try {
            // Use winhttp_api_get directly so we can read 400 response bodies
            // without throwing — needed to detect wrong-secret vs rights errors.
            auto [status, body] = winhttp_api_get(url.c_str(), {
                {"X-App-Id",          m_app_id},
                {"X-User-Auth-Token", auth_token_str}
            });

            if (status == 400) {
                last_error = "HTTP 400: " +
                    (body.size() > 150 ? body.substr(0, 150) + "..." : body);
                continue; // wrong secret or signature — try next
            }
            if (status < 200 || status >= 300) {
                last_error = "HTTP " + std::to_string(status);
                continue;
            }
            if (body.empty()) { last_error = "empty response"; continue; }

            auto j = json::parse(body);

            if (j.contains("status") && j["status"].is_string() &&
                j["status"].get<std::string>() == "error") {
                last_error = j.value("message", "API error");
                continue;
            }

            if (!j.contains("url")) {
                last_error = "no URL in response: " + body.substr(0, 200);
                continue;
            }

            m_secret = sec; // cache the working secret
            return pfc::string8(j["url"].get<std::string>().c_str());

        } catch (std::exception const& e) {
            last_error = e.what();
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

static int jint(const json& obj, const char* key, int def = 0) {
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return def;
    if (it->is_number()) return it->get<int>();
    if (it->is_string()) {
        try { return std::stoi(it->get<std::string>()); } catch (...) {}
    }
    return def;
}

static double jdbl(const json& obj, const char* key, double def = 0.0) {
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return def;
    if (it->is_number()) return it->get<double>();
    if (it->is_string()) {
        try { return std::stod(it->get<std::string>()); } catch (...) {}
    }
    return def;
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
    tr.duration      = jdbl(t, "duration");
    tr.bit_depth     = jint(t, "maximum_bit_depth", 16);
    tr.sampling_rate = jdbl(t, "maximum_sampling_rate", 44.1);
    return tr;
}

// ---- get_track_info ---------------------------------------------------------

QobuzTrack QobuzAPI::get_track_info(const char* track_id, abort_callback& abort) {
    ensure_initialized(abort);

    std::string url =
        std::string("https://www.qobuz.com/api.json/0.2/track/get")
        + "?track_id=" + track_id
        + "&app_id="   + m_app_id;

    auto j = json::parse(do_get(url.c_str(), abort));

    QobuzTrack tr;
    // Identity
    tr.id = track_id;
    tr.title   = jstr(j, "title");
    tr.version = jstr(j, "version");
    if (!tr.version.empty())
        tr.title += " (" + tr.version + ")";

    tr.artist       = jstr_nested(j, "performer", "name");
    tr.composer     = jstr_nested(j, "composer", "name");
    tr.performers   = jstr(j, "performers");
    tr.isrc         = jstr(j, "isrc");
    tr.copyright    = jstr(j, "copyright");

    tr.track_number = jint(j, "track_number");
    tr.disc_number  = jint(j, "media_number", 1);
    tr.duration     = jdbl(j, "duration");
    tr.bit_depth    = jint(j, "maximum_bit_depth", 16);
    tr.sampling_rate = jdbl(j, "maximum_sampling_rate", 44.1);
    tr.channels     = jint(j, "maximum_channel_count", 2);

    // Release date
    tr.date = jstr(j, "release_date_original");

    // ReplayGain
    if (j.contains("audio_info") && j["audio_info"].is_object()) {
        const auto& ai = j["audio_info"];
        if (ai.contains("replaygain_track_gain") && !ai["replaygain_track_gain"].is_null()) {
            tr.rg_track_gain = jdbl(ai, "replaygain_track_gain");
            tr.rg_track_peak = jdbl(ai, "replaygain_track_peak", 1.0);
            tr.has_rg = true;
        }
    }

    // Album sub-object
    if (j.contains("album") && j["album"].is_object()) {
        const auto& alb = j["album"];
        tr.album        = jstr(alb, "title");
        tr.album_id     = jstr(alb, "id");
        tr.album_artist = jstr_nested(alb, "artist", "name");
        tr.genre        = jstr_nested(alb, "genre", "name");
        tr.label        = jstr_nested(alb, "label", "name");
        tr.upc          = jstr(alb, "upc");
        tr.total_tracks = jint(alb, "tracks_count");
        tr.total_discs  = jint(alb, "media_count", 1);
        if (tr.copyright.empty()) tr.copyright = jstr(alb, "copyright");
        if (tr.date.empty())      tr.date       = jstr(alb, "release_date_original");

        // Cover art: prefer "extralarge" (~600px), fall back through smaller sizes
        if (alb.contains("image") && alb["image"].is_object()) {
            const auto& img = alb["image"];
            for (auto key : {"extralarge", "large", "small", "thumbnail"}) {
                std::string u = jstr(img, key);
                if (!u.empty()) { tr.cover_url = u; break; }
            }
        }
    }

    return tr;
}

// ---- search -----------------------------------------------------------------

std::vector<QobuzTrack> QobuzAPI::search_tracks(const char* query, int limit,
                                                  abort_callback& abort) {
    ensure_initialized(abort);
    pfc::string8 encoded;
    pfc::urlEncode(encoded, query);

    std::string url =
        std::string("https://www.qobuz.com/api.json/0.2/catalog/search")
        + "?query="  + encoded.c_str()
        + "&type=tracks"
        + "&limit="  + std::to_string(limit)
        + "&app_id=" + m_app_id
        + "&lang=en&locale=en_US";

    auto j = json::parse(do_get(url.c_str(), abort));
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

    std::string url =
        std::string("https://www.qobuz.com/api.json/0.2/catalog/search")
        + "?query="  + encoded.c_str()
        + "&type=albums"
        + "&limit="  + std::to_string(limit)
        + "&app_id=" + m_app_id
        + "&lang=en&locale=en_US";

    auto j = json::parse(do_get(url.c_str(), abort));
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

    std::string url =
        std::string("https://www.qobuz.com/api.json/0.2/album/get")
        + "?album_id=" + album_id
        + "&app_id="   + m_app_id
        + "&lang=en&locale=en_US";

    auto j = json::parse(do_get(url.c_str(), abort));
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

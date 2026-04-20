#include "stdafx.h"
#include "qobuz_api.h"
#include <nlohmann/json.hpp>
#include <ctime>
#include <cstring>

using json = nlohmann::json;

// ---- advconfig settings --------------------------------------------------

// GUIDs for settings storage – generated once, never change.
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

advconfig_string_factory g_cfg_app_id(
    "App ID",
    "foo_qobuz.app_id", guid_cfg_app_id, guid_advcfg_branch, 1, "");

advconfig_string_factory g_cfg_secret(
    "App Secret",
    "foo_qobuz.secret", guid_cfg_secret, guid_advcfg_branch, 2, "");

// Quality IDs: 5=MP3 320, 6=FLAC 16-bit, 7=FLAC 24-bit ≤96kHz, 27=FLAC 24-bit >96kHz
advconfig_integer_factory g_cfg_quality(
    "Quality (5=MP3, 6=FLAC 16-bit, 7=FLAC 24-bit, 27=FLAC Hi-Res)",
    "foo_qobuz.quality", guid_cfg_quality, guid_advcfg_branch, 3,
    27 /*default*/, 5 /*min*/, 27 /*max*/);

// Expose these for the filesystem and search to read.
advconfig_string_factory& cfg_auth_token() { return g_cfg_auth_token; }
advconfig_string_factory& cfg_app_id()     { return g_cfg_app_id; }
advconfig_string_factory& cfg_secret()     { return g_cfg_secret; }
advconfig_integer_factory& cfg_quality()   { return g_cfg_quality; }

// ---- QobuzAPI implementation ---------------------------------------------

QobuzAPI g_qobuz_api;

void QobuzAPI::add_auth_headers(http_request::ptr& req) {
    pfc::string8 app_id, auth_token;
    g_cfg_app_id.get(app_id);
    g_cfg_auth_token.get(auth_token);
    req->add_header("X-App-Id",          app_id.c_str());
    req->add_header("X-User-Auth-Token", auth_token.c_str());
    req->add_header("User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:83.0) Gecko/20100101 Firefox/83.0");
}

std::string QobuzAPI::do_get(const char* url, abort_callback& abort) {
    auto req = http_client::get()->create_request("GET");
    add_auth_headers(req);

    auto resp = req->run(url, abort);

    std::string body;
    char buf[8192];
    for (;;) {
        size_t got = resp->read(buf, sizeof(buf), abort);
        if (got == 0) break;
        body.append(buf, got);
    }
    return body;
}

pfc::string8 QobuzAPI::get_track_url(const char* track_id, int format_id, abort_callback& abort) {
    pfc::string8 secret_val, app_id_val, auth_token_val;
    g_cfg_secret.get(secret_val);
    if (secret_val.is_empty())
        throw std::runtime_error("Qobuz app secret is not configured");

    g_cfg_app_id.get(app_id_val);
    if (app_id_val.is_empty())
        throw std::runtime_error("Qobuz app ID is not configured");

    g_cfg_auth_token.get(auth_token_val);
    if (auth_token_val.is_empty())
        throw std::runtime_error("Qobuz auth token is not configured");

    long unix_ts = (long)std::time(nullptr);

    // Signature string: trackgetFileUrlformat_id{fmt}intentstreamtrack_id{id}{ts}{secret}
    char sig_input[2048];
    std::snprintf(sig_input, sizeof(sig_input),
        "trackgetFileUrlformat_id%dintentstream"
        "track_id%s%ld%s",
        format_id, track_id, unix_ts, secret_val.c_str());

    // Compute MD5 hex string
    auto md5svc  = hasher_md5::get();
    auto md5res  = md5svc->process_single_string(sig_input);
    pfc::string8 sig_hex = md5res.asString();

    pfc::string8 url;
    url << "https://www.qobuz.com/api.json/0.2/track/getFileUrl"
        << "?track_id="    << track_id
        << "&format_id="   << format_id
        << "&intent=stream"
        << "&request_ts="  << unix_ts
        << "&request_sig=" << sig_hex;

    auto body = do_get(url, abort);
    if (body.empty())
        throw std::runtime_error("Empty response from Qobuz track/getFileUrl");

    auto j = json::parse(body);
    if (!j.contains("url"))
        throw std::runtime_error("No URL in Qobuz track/getFileUrl response");

    return pfc::string8(j["url"].get<std::string>().c_str());
}

// Helper: safely extract a string field from a json object, returning "" on missing/null.
static std::string jstr(const json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return {};
    if (it->is_string()) return it->get<std::string>();
    return {};
}

static std::string jstr_nested(const json& obj, const char* outer, const char* inner) {
    auto it = obj.find(outer);
    if (it == obj.end() || it->is_null() || !it->is_object()) return {};
    return jstr(*it, inner);
}

static QobuzTrack track_from_json(const json& t) {
    QobuzTrack tr;
    if (t.contains("id") && !t["id"].is_null()) {
        if (t["id"].is_string())      tr.id = t["id"].get<std::string>();
        else if (t["id"].is_number()) tr.id = std::to_string(t["id"].get<long long>());
    }
    tr.title         = jstr(t, "title");
    tr.artist        = jstr_nested(t, "performer", "name");
    if (tr.artist.empty()) tr.artist = jstr_nested(t, "artist", "name");
    tr.album         = jstr_nested(t, "album", "title");
    tr.album_id      = jstr_nested(t, "album", "id");
    if (t.contains("duration")             && t["duration"].is_number())
        tr.duration      = t["duration"].get<double>();
    if (t.contains("maximum_bit_depth")    && t["maximum_bit_depth"].is_number())
        tr.bit_depth     = t["maximum_bit_depth"].get<int>();
    if (t.contains("maximum_sampling_rate") && t["maximum_sampling_rate"].is_number())
        tr.sampling_rate = t["maximum_sampling_rate"].get<double>();
    return tr;
}

std::vector<QobuzTrack> QobuzAPI::search_tracks(const char* query, int limit, abort_callback& abort) {
    pfc::string8 encoded_query;
    pfc::urlEncode(encoded_query, query);

    pfc::string8 url;
    url << "https://www.qobuz.com/api.json/0.2/track/search"
        << "?query="  << encoded_query
        << "&limit="  << limit;

    auto body = do_get(url, abort);
    auto j = json::parse(body);

    std::vector<QobuzTrack> results;
    if (!j.contains("tracks") || !j["tracks"].contains("items")) return results;
    for (auto& t : j["tracks"]["items"]) {
        if (t.is_null()) continue;
        results.push_back(track_from_json(t));
    }
    return results;
}

std::vector<QobuzAlbum> QobuzAPI::search_albums(const char* query, int limit, abort_callback& abort) {
    pfc::string8 encoded_query;
    pfc::urlEncode(encoded_query, query);

    pfc::string8 url;
    url << "https://www.qobuz.com/api.json/0.2/album/search"
        << "?query=" << encoded_query
        << "&limit=" << limit;

    auto body = do_get(url, abort);
    auto j = json::parse(body);

    std::vector<QobuzAlbum> results;
    if (!j.contains("albums") || !j["albums"].contains("items")) return results;
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
        results.push_back(al);
    }
    return results;
}

std::vector<QobuzTrack> QobuzAPI::get_album_tracks(const char* album_id, abort_callback& abort) {
    pfc::string8 url;
    url << "https://www.qobuz.com/api.json/0.2/album/get"
        << "?album_id=" << album_id;

    auto body = do_get(url, abort);
    auto j = json::parse(body);

    std::vector<QobuzTrack> results;
    if (!j.contains("tracks") || !j["tracks"].contains("items")) return results;

    // Top-level album fields for filling in track details
    std::string album_title  = jstr(j, "title");
    std::string album_artist = jstr_nested(j, "artist", "name");

    for (auto& t : j["tracks"]["items"]) {
        if (t.is_null()) continue;
        QobuzTrack tr = track_from_json(t);
        if (tr.album.empty())  tr.album  = album_title;
        if (tr.artist.empty()) tr.artist = album_artist;
        tr.album_id = album_id;
        results.push_back(tr);
    }
    return results;
}

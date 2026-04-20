#pragma once

#include "stdafx.h"
#include "qobuz_bundle.h"
#include <mutex>
#include <string>
#include <vector>

struct QobuzTrack {
    std::string id, title, artist, album, album_id;
    double duration = 0.0;
    int    bit_depth = 16;
    double sampling_rate = 44.1;
};

struct QobuzAlbum {
    std::string id, title, artist;
    int tracks_count = 0, year = 0;
};

class QobuzAPI {
public:
    pfc::string8 get_track_url(const char* track_id, int format_id, abort_callback& abort);
    std::vector<QobuzTrack> search_tracks(const char* query, int limit, abort_callback& abort);
    std::vector<QobuzAlbum> search_albums(const char* query, int limit, abort_callback& abort);
    std::vector<QobuzTrack> get_album_tracks(const char* album_id, abort_callback& abort);

private:
    std::string              m_app_id;
    std::vector<std::string> m_secrets;
    std::string              m_secret;   // last known-good secret (cache)
    std::mutex               m_init_mutex;
    bool                     m_initialized = false;

    void ensure_initialized(abort_callback& abort);
    std::string do_get(const char* url, abort_callback& abort);
    void add_auth_headers(http_request::ptr& req);
};

extern QobuzAPI g_qobuz_api;

advconfig_string_factory&  cfg_auth_token();
advconfig_string_factory&  cfg_app_id();
advconfig_string_factory&  cfg_secret();
advconfig_integer_factory& cfg_quality();

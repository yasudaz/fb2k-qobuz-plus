// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Carl Kittelberger <icedream@icedream.pw>

#pragma once

#include "stdafx.h"
#include "qobuz_bundle.h"
#include <mutex>
#include <string>
#include <vector>

struct QobuzTrack {
    // Core identity (populated by search results and track/get)
    std::string id, title, artist, album, album_id;
    double duration       = 0.0;
    int    bit_depth      = 16;
    double sampling_rate  = 44.1;

    // Extended metadata (populated by get_track_info() via track/get)
    std::string version;        // e.g. "Radio Edit"
    std::string album_artist;
    std::string date;           // YYYY-MM-DD from release_date_original
    std::string genre;
    std::string label;
    std::string isrc;
    std::string copyright;
    std::string upc;
    std::string composer;
    std::string performers;     // raw role string from API
    std::string cover_url;      // HTTPS URL to album cover JPEG (600px)
    int         track_number  = 0;
    int         total_tracks  = 0;
    int         disc_number   = 1;
    int         total_discs   = 1;
    int         channels      = 2;
    double      rg_track_gain = 0.0;
    double      rg_track_peak = 1.0;
    bool        has_rg        = false;
};

struct QobuzAlbum {
    std::string id, title, artist;
    int         tracks_count = 0, year = 0;
    int         bit_depth      = 16;
    double      sampling_rate  = 44.1;
};

class QobuzAPI {
public:
    pfc::string8 get_track_url(const char* track_id, int format_id, abort_callback& abort);
    QobuzTrack   get_track_info(const char* track_id, abort_callback& abort);
    std::string  download_url(const char* url);   // unauthenticated GET (CDN images etc.)
    std::vector<QobuzTrack> search_tracks(const char* query, int limit, abort_callback& abort);
    std::vector<QobuzAlbum> search_albums(const char* query, int limit, abort_callback& abort);
    std::vector<QobuzTrack> get_album_tracks(const char* album_id, abort_callback& abort);
    std::vector<QobuzTrack> get_playlist_tracks(const char* playlist_id, abort_callback& abort);

private:
    std::string              m_app_id;
    std::vector<std::string> m_secrets;
    std::string              m_secret;   // last known-good secret (cache)
    std::mutex               m_init_mutex;
    bool                     m_initialized = false;

    void ensure_initialized(abort_callback& abort);
    std::string do_get(const char* url, abort_callback& abort);
};

extern QobuzAPI g_qobuz_api;

advconfig_string_factory&  cfg_auth_token();
advconfig_string_factory&  cfg_app_id();
advconfig_string_factory&  cfg_secret();
advconfig_integer_factory& cfg_quality();

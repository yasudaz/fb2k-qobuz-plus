#pragma once

#include "stdafx.h"

// Track info returned by search or album enumeration
struct QobuzTrack {
    std::string id;
    std::string title;
    std::string artist;
    std::string album;
    std::string album_id;
    double      duration      = 0.0;
    int         bit_depth     = 16;
    double      sampling_rate = 44.1;
};

// Album info returned by search
struct QobuzAlbum {
    std::string id;
    std::string title;
    std::string artist;
    int         tracks_count = 0;
    int         year         = 0;
};

class QobuzAPI {
public:
    // Returns the direct HTTPS streaming URL for a given track ID and format.
    pfc::string8 get_track_url(const char* track_id, int format_id, abort_callback& abort);

    // Search overloads
    std::vector<QobuzTrack> search_tracks(const char* query, int limit, abort_callback& abort);
    std::vector<QobuzAlbum> search_albums(const char* query, int limit, abort_callback& abort);

    // Retrieve all tracks in an album
    std::vector<QobuzTrack> get_album_tracks(const char* album_id, abort_callback& abort);

private:
    // Perform a generic GET call and return the response body as a string.
    std::string do_get(const char* url, abort_callback& abort);

    // Add authentication / app-id headers to a request.
    void add_auth_headers(http_request::ptr& req);
};

// Singleton used by the rest of the plugin
extern QobuzAPI g_qobuz_api;

// Advconfig accessors - defined in qobuz_api.cpp, used by filesystem + search
advconfig_string_factory&  cfg_auth_token();
advconfig_string_factory&  cfg_app_id();
advconfig_string_factory&  cfg_secret();
advconfig_integer_factory& cfg_quality();

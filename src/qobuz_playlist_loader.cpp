// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Carl Kittelberger <icedream@icedream.pw>

#include "stdafx.h"
#include "qobuz_api.h"

// ---- Shared URL helpers ----------------------------------------------------

// Returns the path portion of an HTTPS URL on a known Qobuz domain, or nullptr.
static const char* qobuz_url_path(const char* url) {
    if (!pfc::string_has_prefix(url, "https://")) return nullptr;
    const char* after_scheme = url + 8;
    for (const char* host : { "play.qobuz.com", "open.qobuz.com", "www.qobuz.com" }) {
        size_t hlen = std::strlen(host);
        if (std::strncmp(after_scheme, host, hlen) == 0 &&
            (after_scheme[hlen] == '/' || after_scheme[hlen] == '\0'))
            return after_scheme + hlen;
    }
    return nullptr;
}

// Returns the first path segment after the given prefix segment (e.g. "/album/"),
// stripping any trailing query string or fragment. Returns empty string if not found.
static std::string segment_after(const char* path, const char* needle) {
    const char* seg = std::strstr(path, needle);
    if (!seg) return {};
    const char* p = seg + std::strlen(needle);
    const char* end = p;
    while (*end && *end != '/' && *end != '?' && *end != '#') ++end;
    if (end == p) return {};
    return std::string(p, end - p);
}

// ---- Qobuz playlist URL patterns -------------------------------------------
//
// Supported forms:
//   https://play.qobuz.com/playlist/<id>
//   https://open.qobuz.com/playlist/<id>
//   https://www.qobuz.com/<locale>/playlists/<slug>/<id>
//
// The playlist ID is always the last numeric path segment.

static std::string parse_qobuz_playlist_id(const char* path) {
    const char* url_path = qobuz_url_path(path);
    if (!url_path) return {};

    const char* seg = std::strstr(url_path, "/playlist/");
    if (!seg) seg = std::strstr(url_path, "/playlists/");
    if (!seg) return {};

    // Walk forward past any slug segments to find the last purely-numeric segment.
    const char* p = seg + 1;
    const char* slash = std::strchr(p, '/');
    if (!slash) return {};
    p = slash + 1;

    std::string last_numeric;
    while (*p) {
        const char* end = p;
        while (*end && *end != '/' && *end != '?' && *end != '#') ++end;

        bool numeric = (end > p);
        for (const char* c = p; c < end && numeric; ++c)
            numeric = (*c >= '0' && *c <= '9');
        if (numeric)
            last_numeric.assign(p, end - p);

        if (*end == '\0' || *end == '?' || *end == '#') break;
        p = end + 1;
    }
    return last_numeric;
}

// ---- Qobuz album URL patterns ----------------------------------------------
//
// Supported forms:
//   https://play.qobuz.com/album/<id>
//   https://open.qobuz.com/album/<id>
//
// Album IDs are alphanumeric slugs (e.g. "hzhzsr05oisxc").

static std::string parse_qobuz_album_id(const char* path) {
    const char* url_path = qobuz_url_path(path);
    if (!url_path) return {};
    return segment_after(url_path, "/album/");
}

// ---- playlist_loader implementation ----------------------------------------

class qobuz_playlist_loader : public playlist_loader {
public:
    // Called by foobar2000 when a file with matching content type or extension
    // is being loaded as a playlist. We intercept text/html responses from
    // known Qobuz domains, ignore everything else.
    void open(const char* p_path, const service_ptr_t<file>& /*p_file*/,
              playlist_loader_callback::ptr p_callback,
              abort_callback& p_abort) override
    {
        std::vector<QobuzTrack> tracks;

        std::string playlist_id = parse_qobuz_playlist_id(p_path);
        std::string album_id    = parse_qobuz_album_id(p_path);

        if (!playlist_id.empty())
            tracks = g_qobuz_api.get_playlist_tracks(playlist_id.c_str(), p_abort);
        else if (!album_id.empty())
            tracks = g_qobuz_api.get_album_tracks(album_id.c_str(), p_abort);
        else
            throw exception_io_unsupported_format();

        for (const auto& track : tracks) {
            p_abort.check();

            pfc::string8 uri;
            uri << "qobuz://track/" << track.id.c_str();

            metadb_handle_ptr handle;
            p_callback->handle_create(handle, make_playable_location(uri, 0));

            // Provide whatever metadata we already have from the playlist
            // response (title, artist, album, duration) so the playlist
            // populates immediately without extra API calls per track.
            file_info_impl info;
            if (!track.title.empty())  info.meta_add("TITLE",  track.title.c_str());
            if (!track.artist.empty()) info.meta_add("ARTIST", track.artist.c_str());
            if (!track.album.empty())  info.meta_add("ALBUM",  track.album.c_str());
            if (track.duration > 0)    info.set_length(track.duration);

            p_callback->on_entry_info(handle,
                playlist_loader_callback::entry_user_requested,
                filestats_invalid, info, /*fresh=*/false);
        }
    }

    void write(const char* /*p_path*/, const service_ptr_t<file>& /*p_file*/,
               metadb_handle_list_cref /*p_data*/,
               abort_callback& /*p_abort*/) override
    {
        throw pfc::exception_not_implemented();
    }

    // Return an empty extension so this loader is never matched by file extension.
    // We only fire through is_our_content_type().
    const char* get_extension() override { return ""; }

    bool can_write() override { return false; }

    // Intercept text/html responses — Qobuz playlist pages are HTML.
    // open() will reject non-Qobuz URLs immediately via exception_io_unsupported_format.
    bool is_our_content_type(const char* p_content_type) override {
        return pfc::string_has_prefix_i(p_content_type, "text/html");
    }

    bool is_associatable() override { return false; }
};

static playlist_loader_factory_t<qobuz_playlist_loader> g_qobuz_playlist_loader;

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Carl Kittelberger <icedream@icedream.pw>

#include "stdafx.h"
#include "qobuz_api.h"

// ---- Qobuz playlist URL patterns -------------------------------------------
//
// Supported forms:
//   https://play.qobuz.com/playlist/<id>
//   https://open.qobuz.com/playlist/<id>
//   https://www.qobuz.com/<locale>/playlists/<slug>/<id>
//
// The playlist ID is always the last numeric path segment.

static std::string parse_qobuz_playlist_id(const char* path) {
    // Must be an HTTPS URL on a known Qobuz domain
    if (!pfc::string_has_prefix(path, "https://"))
        return {};

    const char* after_scheme = path + 8; // skip "https://"
    // Match known hosts
    bool known_host = false;
    const char* path_start = nullptr;
    for (const char* host : { "play.qobuz.com", "open.qobuz.com", "www.qobuz.com" }) {
        size_t hlen = std::strlen(host);
        if (std::strncmp(after_scheme, host, hlen) == 0 &&
            (after_scheme[hlen] == '/' || after_scheme[hlen] == '\0')) {
            known_host = true;
            path_start = after_scheme + hlen;
            break;
        }
    }
    if (!known_host || !path_start) return {};

    // Path must contain "/playlist/" or "/playlists/"
    const char* seg = std::strstr(path_start, "/playlist/");
    if (!seg) seg = std::strstr(path_start, "/playlists/");
    if (!seg) return {};

    // Skip to the last path segment (the numeric ID)
    // Walk forward past any slug segments to find a purely numeric segment.
    const char* p = seg + 1; // skip leading '/'
    // skip "playlist(s)/"
    const char* slash = std::strchr(p, '/');
    if (!slash) return {};
    p = slash + 1; // now at the first segment after "playlist(s)"

    // Scan all remaining segments; the ID is the last numeric one.
    std::string last_numeric;
    while (*p) {
        // Find segment end
        const char* end = p;
        while (*end && *end != '/' && *end != '?' && *end != '#') ++end;

        // Check if this segment is purely numeric
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
        std::string playlist_id = parse_qobuz_playlist_id(p_path);
        if (playlist_id.empty())
            throw exception_io_unsupported_format();

        auto tracks = g_qobuz_api.get_playlist_tracks(playlist_id.c_str(), p_abort);
        if (tracks.empty()) return;

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

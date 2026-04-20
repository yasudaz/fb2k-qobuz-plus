#include "stdafx.h"
#include "qobuz_api.h"

// ---- qobuz:// filesystem provider ---------------------------------------
//
// Handles URIs of the form:  qobuz://track/TRACK_ID
//
// When foobar2000 (or an input decoder) opens such a URI we call the Qobuz
// API to retrieve a time-limited HTTPS streaming URL and delegate the actual
// I/O to the built-in HTTP filesystem.  The native FLAC/AAC/MP3 inputs then
// decode and present the audio transparently.

namespace {

// Extract the track-id portion from "qobuz://track/12345678"
static pfc::string8 parse_track_id(const char* path) {
    // path begins with "qobuz://track/"
    const char* prefix = "qobuz://track/";
    const size_t prefix_len = std::strlen(prefix);
    if (std::strncmp(path, prefix, prefix_len) != 0)
        throw exception_io_data("Invalid qobuz:// URI");
    return pfc::string8(path + prefix_len);
}

class qobuz_filesystem_impl : public filesystem {
public:
    bool get_canonical_path(const char* path, pfc::string_base& out) override {
        out = path;
        return true;
    }

    bool is_our_path(const char* path) override {
        return pfc::string_has_prefix(path, "qobuz://");
    }

    bool get_display_path(const char* path, pfc::string_base& out) override {
        out = path;
        return true;
    }

    bool is_remote(const char*) override { return true; }

    bool supports_content_types() override { return false; }

    // The only interesting operation: resolve qobuz://track/ID → HTTP stream.
    void open(file::ptr& p_out, const char* path, t_open_mode mode, abort_callback& abort) override {
        if (mode != open_mode_read)
            throw exception_io_denied();

        auto track_id  = parse_track_id(path);
        int  format_id = (int)cfg_quality().get();

        pfc::string8 stream_url = g_qobuz_api.get_track_url(track_id, format_id, abort);

        // Delegate to the built-in HTTP filesystem.
        filesystem::g_open(p_out, stream_url, open_mode_read, abort);
    }

    void get_stats(const char*, t_filestats& stats, bool& writable, abort_callback&) override {
        stats.m_size      = filesize_invalid;
        stats.m_timestamp = filetimestamp_invalid;
        writable          = false;
    }

    void remove(const char*, abort_callback&) override { throw exception_io_denied(); }
    void move(const char*, const char*, abort_callback&) override { throw exception_io_denied(); }
    void create_directory(const char*, abort_callback&) override { throw exception_io_denied(); }
    void list_directory(const char*, directory_callback&, abort_callback&) override {
        throw exception_io_denied();
    }
};

// Hook into foobar2000's service registry so the core finds our filesystem.
static service_factory_single_t<qobuz_filesystem_impl> g_qobuz_filesystem_factory;

} // anonymous namespace


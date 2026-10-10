// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Carl Kittelberger <icedream@icedream.pw>
// SPDX-FileCopyrightText: 2026 yasudaz <https://github.com/yasudaz>

#include "stdafx.h"
#include "qobuz_api.h"
#include "resource.h"

#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <gdiplus.h>
#include "qobuz_html_view.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>

// GUIDs for menu registration
static constexpr GUID guid_mainmenu_group =
    { 0x5a7b3a0c, 0x1f8e, 0x4d2b, { 0xa3, 0x51, 0x7c, 0x9e, 0x2d, 0x4b, 0x6f, 0x88 } };
static constexpr GUID guid_cmd_search =
    { 0x8e2f1b9d, 0x4c7a, 0x4e3f, { 0xb2, 0x64, 0x1a, 0x8d, 0x5c, 0x3e, 0x7b, 0x95 } };
static constexpr GUID guid_cmd_search_albums =
    { 0xc4a1d7e2, 0x3b8f, 0x4c9a, { 0x85, 0x7d, 0x2f, 0x6b, 0x4e, 0x1c, 0x9a, 0x3d } };

// ---- Playlist helpers ----------------------------------------------------

// Forward declaration of advconfig accessor from qobuz_api.cpp is in qobuz_api.h

static void add_tracks_to_playlist(const std::vector<QobuzTrack>& tracks, bool play_first) {
    if (tracks.empty()) return;

    pfc::list_t<metadb_handle_ptr> items;
    auto hint_list = metadb_hint_list::create();

    for (auto& t : tracks) {
        pfc::string8 path;
        path << "qobuz://track/" << t.id.c_str();
        metadb_handle_ptr h;
        metadb::get()->handle_create(h, make_playable_location(path, 0));
        items.add_item(h);

        file_info_impl info;
        if (!t.title.empty())        info.meta_add("TITLE",        t.title.c_str());
        if (!t.artist.empty())       info.meta_add("ARTIST",       t.artist.c_str());
        if (!t.album_artist.empty()) info.meta_add("ALBUM ARTIST", t.album_artist.c_str());
        if (!t.album.empty())        info.meta_add("ALBUM",        t.album.c_str());
        if (t.track_number > 0)      info.meta_add("TRACKNUMBER",  std::to_string(t.track_number).c_str());
        if (t.total_tracks > 0)      info.meta_add("TOTALTRACKS",  std::to_string(t.total_tracks).c_str());
        if (t.disc_number > 0)       info.meta_add("DISCNUMBER",   std::to_string(t.disc_number).c_str());
        if (t.total_discs > 1)       info.meta_add("TOTALDISCS",   std::to_string(t.total_discs).c_str());
        if (!t.date.empty())         info.meta_add("DATE",         t.date.c_str());
        if (!t.genre.empty())        info.meta_add("GENRE",        t.genre.c_str());
        if (!t.composer.empty())     info.meta_add("COMPOSER",     t.composer.c_str());
        if (!t.label.empty())        info.meta_add("LABEL",        t.label.c_str());
        if (!t.isrc.empty())         info.meta_add("ISRC",         t.isrc.c_str());
        if (!t.copyright.empty())    info.meta_add("COPYRIGHT",    t.copyright.c_str());
        if (!t.upc.empty())          info.meta_add("UPC",          t.upc.c_str());
        if (!t.performers.empty())   info.meta_add("PERFORMERS",   t.performers.c_str());

        if (t.has_rg) {
            pfc::string8 gain_str, peak_str;
            gain_str << t.rg_track_gain << " dB";
            peak_str << t.rg_track_peak;
            info.meta_add("REPLAYGAIN_TRACK_GAIN", gain_str);
            info.meta_add("REPLAYGAIN_TRACK_PEAK", peak_str);
        }

        info.set_length(t.duration);
        if (t.sampling_rate > 0) info.info_set_int("samplerate", (t_int64)(t.sampling_rate * 1000.0 + 0.5));
        if (t.bit_depth > 0)     info.info_set_int("bitspersample", t.bit_depth);
        if (t.channels > 0)      info.info_set_int("channels", t.channels);
        info.info_set("codec",    (int)cfg_quality().get() >= 27 ? "FLAC" : "AAC");
        info.info_set("encoding", "lossless");

        hint_list->add_hint(h, info, filestats_invalid, true);
    }

    hint_list->on_done();

    auto pm = playlist_manager::get();
    t_size pl = pm->get_active_playlist();
    if (pl == pfc_infinite) {
        pm->create_playlist("Qobuz", pfc_infinite, pl);
        pl = 0;
    }

    t_size insert_pos = pm->playlist_get_item_count(pl);
    pm->playlist_add_items(pl, items, pfc::bit_array_false());

    if (play_first) {
        pm->set_active_playlist(pl);
        playback_control::get()->play_start(playback_control::track_command_settrack);
        pm->playlist_execute_default_action(pl, insert_pos);
    }
}

// ---- Search dialog -------------------------------------------------------

// WM_APP messages for cross-thread communication
#define WM_SEARCH_RESULTS  (WM_APP + 1)  // LPARAM = new std::vector<QobuzTrack>*
#define WM_SEARCH_ALBUMS   (WM_APP + 2)  // LPARAM = new std::vector<QobuzAlbum>*
#define WM_SEARCH_STATUS   (WM_APP + 3)  // LPARAM = new std::string* (status text)
#define WM_SEARCH_ERROR    (WM_APP + 4)  // LPARAM = new std::string* (error text)
#define WM_ALBUM_DETAILS   (WM_APP + 5)  // LPARAM = new QobuzAlbumDetails*
#define WM_TRACK_DETAILS   (WM_APP + 6)  // LPARAM = new QobuzTrackDetails*
#define WM_ART_LOADED      (WM_APP + 7)  // LPARAM = new std::string* (raw image data)

struct SearchState {
    HWND  hwnd        = nullptr;
    std::atomic<bool> cancel { false };
};

// Shared state for the single open search dialog (there can only be one)
static SearchState g_search_state;
static HWND        g_search_hwnd = nullptr;

// Column indices for track list
enum TrackCol { COL_TITLE=0, COL_ARTIST, COL_ALBUM, COL_DURATION, COL_HIRES, COL_QUALITY, COL_DATE, COL_ID };
enum AlbumCol { COL_A_TITLE=0, COL_A_ARTIST, COL_A_TRACKS, COL_A_YEAR, COL_A_HIRES, COL_A_QUALITY, COL_A_ID };

enum SearchMode { MODE_TRACKS, MODE_ALBUMS };

struct DialogData {
    SearchMode mode = MODE_ALBUMS;
    bool filter_hires = false;
    std::vector<QobuzTrack>  raw_track_results;
    std::vector<QobuzAlbum>  raw_album_results;
    std::vector<QobuzTrack>  track_results;
    std::vector<QobuzAlbum>  album_results;
    // For album-drill-down: current album_id being expanded
    std::string expand_album_id;

    int sort_column = -1;
    bool sort_ascending = true;
};

static int calc_col_width(HWND lv, int char_count) {
    HDC hdc = GetDC(lv);
    if (!hdc) return char_count * 8 + 16;
    HFONT hFont = (HFONT)SendMessageW(lv, WM_GETFONT, 0, 0);
    HFONT hOldFont = nullptr;
    if (hFont) hOldFont = (HFONT)SelectObject(hdc, hFont);

    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);
    SIZE szZero = {};
    GetTextExtentPoint32W(hdc, L"0", 1, &szZero);
    int char_w = (std::max)((int)tm.tmAveCharWidth, (int)szZero.cx);
    if (char_w <= 0) char_w = 8;

    if (hOldFont) SelectObject(hdc, hOldFont);
    ReleaseDC(lv, hdc);

    return char_w * char_count + 16;
}

static void setup_track_columns(HWND lv) {
    ListView_DeleteAllItems(lv);
    while (ListView_DeleteColumn(lv, 0)) {}

    auto add_col = [&](const wchar_t* name, int width) {
        LVCOLUMNW col = {};
        col.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.fmt     = LVCFMT_LEFT;
        col.cx      = width;
        col.pszText = (LPWSTR)name;
        ListView_InsertColumn(lv, ListView_GetHeader(lv) ? Header_GetItemCount(ListView_GetHeader(lv)) : 0, &col);
    };
    add_col(L"Title",    calc_col_width(lv, 30));
    add_col(L"Artist",   calc_col_width(lv, 20));
    add_col(L"Album",    calc_col_width(lv, 20));
    add_col(L"Duration", calc_col_width(lv, 8));
    add_col(L"Hi-Res",   calc_col_width(lv, 6));
    add_col(L"Quality",  calc_col_width(lv, 10));
    add_col(L"Date",     calc_col_width(lv, 10));
    add_col(L"ID",       0);  // Hidden: used to retrieve track id
}

static void setup_album_columns(HWND lv) {
    ListView_DeleteAllItems(lv);
    while (ListView_DeleteColumn(lv, 0)) {}

    auto add_col = [&](const wchar_t* name, int width) {
        LVCOLUMNW col = {};
        col.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.fmt     = LVCFMT_LEFT;
        col.cx      = width;
        col.pszText = (LPWSTR)name;
        ListView_InsertColumn(lv, Header_GetItemCount(ListView_GetHeader(lv)), &col);
    };
    add_col(L"Title",   calc_col_width(lv, 30));
    add_col(L"Artist",  calc_col_width(lv, 20));
    add_col(L"Tracks",  calc_col_width(lv, 6));
    add_col(L"Year",    calc_col_width(lv, 4));
    add_col(L"Hi-Res",  calc_col_width(lv, 6));
    add_col(L"Quality", calc_col_width(lv, 10));
    add_col(L"ID",      0);
}

static void update_header_sort_icon(HWND lv, int sort_col, bool ascending) {
    HWND header = ListView_GetHeader(lv);
    if (!header) return;

    int count = Header_GetItemCount(header);
    for (int i = 0; i < count; ++i) {
        HDITEMW hdi = {};
        hdi.mask = HDI_FORMAT;
        Header_GetItem(header, i, &hdi);

        hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == sort_col) hdi.fmt |= ascending ? HDF_SORTUP : HDF_SORTDOWN;
        Header_SetItem(header, i, &hdi);
    }
}

// Utility: UTF-8 → wchar_t for ListView text
static std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    pfc::stringcvt::string_wide_from_utf8 cvt(s.c_str());
    return std::wstring(cvt.get_ptr());
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

static void set_lv_item_text(HWND lv, int row, int col, const std::wstring& text) {
    LVITEMW item = {};
    item.mask      = LVIF_TEXT;
    item.iItem     = row;
    item.iSubItem  = col;
    item.pszText   = (LPWSTR)text.c_str();
    if (col == 0)
        ListView_InsertItem(lv, &item);
    else
        ListView_SetItem(lv, &item);
}

static std::wstring format_duration(double secs) {
    int s = (int)secs;
    wchar_t buf[32];
    _snwprintf_s(buf, 32, L"%d:%02d", s / 60, s % 60);
    return buf;
}

static std::wstring format_quality_str(int bit_depth, double sampling_rate) {
    if (bit_depth == 0 || sampling_rate == 0.0) return L"";
    wchar_t buf[32];
    if (sampling_rate == std::floor(sampling_rate))
        _snwprintf_s(buf, 32, L"%dbit/%.0fkHz", bit_depth, sampling_rate);
    else
        _snwprintf_s(buf, 32, L"%dbit/%.1fkHz", bit_depth, sampling_rate);
    return buf;
}

static std::wstring format_quality(const QobuzTrack& t) {
    return format_quality_str(t.bit_depth, t.sampling_rate);
}

// ---- GDI+ Image Helpers --------------------------------------------------

static ULONG_PTR g_gdiplusToken = 0;
static void ensure_gdiplus() {
    static std::once_flag flag;
    std::call_once(flag, []() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr);
    });
}

static Gdiplus::Bitmap* load_bitmap_from_memory(const void* data, size_t size) {
    if (!data || size == 0) return nullptr;
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hGlobal) return nullptr;
    void* pMem = GlobalLock(hGlobal);
    if (!pMem) {
        GlobalFree(hGlobal);
        return nullptr;
    }
    memcpy(pMem, data, size);
    GlobalUnlock(hGlobal);

    IStream* pStream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hGlobal, TRUE, &pStream))) {
        GlobalFree(hGlobal);
        return nullptr;
    }
    ensure_gdiplus();
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(pStream);
    pStream->Release();
    if (bmp && bmp->GetLastStatus() != Gdiplus::Ok) {
        delete bmp;
        return nullptr;
    }
    return bmp;
}

static void draw_bitmap_fitted(HDC hdc, Gdiplus::Bitmap* bmp, const RECT& rcDest) {
    if (!bmp) return;
    int destW = rcDest.right - rcDest.left;
    int destH = rcDest.bottom - rcDest.top;
    if (destW <= 0 || destH <= 0) return;

    int bmpW = bmp->GetWidth();
    int bmpH = bmp->GetHeight();
    if (bmpW <= 0 || bmpH <= 0) return;

    float scale = (std::min)((float)destW / bmpW, (float)destH / bmpH);
    int drawW = (int)(bmpW * scale);
    int drawH = (int)(bmpH * scale);
    int drawX = rcDest.left + (destW - drawW) / 2;
    int drawY = rcDest.top + (destH - drawH) / 2;

    ensure_gdiplus();
    Gdiplus::Graphics g(hdc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(bmp, drawX, drawY, drawW, drawH);
}

static void populate_tracks(HWND lv, const std::vector<QobuzTrack>& tracks) {
    ListView_DeleteAllItems(lv);
    int row = 0;
    for (auto& t : tracks) {
        set_lv_item_text(lv, row, COL_TITLE,    to_wide(t.title));
        set_lv_item_text(lv, row, COL_ARTIST,   to_wide(t.artist));
        set_lv_item_text(lv, row, COL_ALBUM,    to_wide(t.album));
        set_lv_item_text(lv, row, COL_DURATION, format_duration(t.duration));
        set_lv_item_text(lv, row, COL_HIRES,    (t.bit_depth > 16 || t.sampling_rate > 44.1) ? L"Hi-Res" : L"");
        set_lv_item_text(lv, row, COL_QUALITY,  format_quality(t));
        set_lv_item_text(lv, row, COL_DATE,     to_wide(t.date));
        set_lv_item_text(lv, row, COL_ID,       to_wide(t.id));
        ++row;
    }
}

static void populate_albums(HWND lv, const std::vector<QobuzAlbum>& albums) {
    ListView_DeleteAllItems(lv);
    int row = 0;
    for (auto& a : albums) {
        set_lv_item_text(lv, row, COL_A_TITLE,  to_wide(a.title));
        set_lv_item_text(lv, row, COL_A_ARTIST, to_wide(a.artist));
        wchar_t cnt[16]; _snwprintf_s(cnt, 16, L"%d", a.tracks_count);
        set_lv_item_text(lv, row, COL_A_TRACKS, cnt);
        wchar_t yr[16] = {}; if (a.year > 0) _snwprintf_s(yr, 16, L"%d", a.year);
        set_lv_item_text(lv, row, COL_A_YEAR,   yr);
        set_lv_item_text(lv, row, COL_A_HIRES,  (a.bit_depth > 16 || a.sampling_rate > 44.1) ? L"Hi-Res" : L"");
        set_lv_item_text(lv, row, COL_A_QUALITY, format_quality_str(a.bit_depth, a.sampling_rate));
        set_lv_item_text(lv, row, COL_A_ID,     to_wide(a.id));
        ++row;
    }
}

// Get the track ID at a given listview row (stored in the hidden ID column)
static std::string get_lv_track_id(HWND lv, int row) {
    wchar_t buf[64] = {};
    LVITEMW item = {};
    item.mask      = LVIF_TEXT;
    item.iItem     = row;
    item.iSubItem  = COL_ID;
    item.pszText   = buf;
    item.cchTextMax = (int)std::size(buf);
    ListView_GetItem(lv, &item);
    // Convert back to UTF-8
    pfc::stringcvt::string_utf8_from_wide cvt(buf);
    return cvt.get_ptr();
}

static std::string get_lv_album_id(HWND lv, int row) {
    wchar_t buf[256] = {};
    LVITEMW item = {};
    item.mask       = LVIF_TEXT;
    item.iItem      = row;
    item.iSubItem   = COL_A_ID;
    item.pszText    = buf;
    item.cchTextMax = (int)std::size(buf);
    ListView_GetItem(lv, &item);
    pfc::stringcvt::string_utf8_from_wide cvt(buf);
    return cvt.get_ptr();
    char narrow[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, narrow, sizeof(narrow), nullptr, nullptr);
    return narrow;
}

static bool is_hires(const QobuzTrack& t) {
    return t.bit_depth > 16 || t.sampling_rate > 44.1;
}

static bool is_hires(const QobuzAlbum& a) {
    return a.bit_depth > 16 || a.sampling_rate > 44.1;
}

static void sort_and_refresh_tracks(HWND hwnd, DialogData* dd) {
    if (!dd) return;
    HWND lv = GetDlgItem(hwnd, IDC_RESULTS_LIST);
    dd->track_results.clear();
    for (const auto& t : dd->raw_track_results) {
        if (!dd->filter_hires || is_hires(t)) {
            dd->track_results.push_back(t);
        }
    }

    if (dd->sort_column >= 0) {
        auto col = dd->sort_column;
        auto asc = dd->sort_ascending;
        std::stable_sort(dd->track_results.begin(), dd->track_results.end(), [col, asc](const QobuzTrack& a, const QobuzTrack& b) {
            int cmp = 0;
            if (col == COL_TITLE) cmp = _stricmp(a.title.c_str(), b.title.c_str());
            else if (col == COL_ARTIST) cmp = _stricmp(a.artist.c_str(), b.artist.c_str());
            else if (col == COL_ALBUM) cmp = _stricmp(a.album.c_str(), b.album.c_str());
            else if (col == COL_DURATION) cmp = (a.duration < b.duration) ? -1 : (a.duration > b.duration ? 1 : 0);
            else if (col == COL_HIRES) {
                bool a_hr = is_hires(a);
                bool b_hr = is_hires(b);
                cmp = (a_hr == b_hr) ? 0 : (a_hr ? 1 : -1);
            }
            else if (col == COL_QUALITY) {
                if (a.bit_depth != b.bit_depth) cmp = a.bit_depth < b.bit_depth ? -1 : 1;
                else cmp = (a.sampling_rate < b.sampling_rate) ? -1 : (a.sampling_rate > b.sampling_rate ? 1 : 0);
            }
            else if (col == COL_DATE) cmp = _stricmp(a.date.c_str(), b.date.c_str());

            if (cmp == 0) return false;
            return asc ? (cmp < 0) : (cmp > 0);
        });
    }

    populate_tracks(lv, dd->track_results);
    update_header_sort_icon(lv, dd->sort_column, dd->sort_ascending);

    wchar_t status[64];
    _snwprintf_s(status, 64, L"%d track(s) found.", (int)dd->track_results.size());
    SetDlgItemTextW(hwnd, IDC_STATUS_TEXT, status);
}

static void sort_and_refresh_albums(HWND hwnd, DialogData* dd) {
    if (!dd) return;
    HWND lv = GetDlgItem(hwnd, IDC_RESULTS_LIST);
    dd->album_results.clear();
    for (const auto& a : dd->raw_album_results) {
        if (!dd->filter_hires || is_hires(a)) {
            dd->album_results.push_back(a);
        }
    }

    if (dd->sort_column >= 0) {
        auto col = dd->sort_column;
        auto asc = dd->sort_ascending;
        std::stable_sort(dd->album_results.begin(), dd->album_results.end(), [col, asc](const QobuzAlbum& a, const QobuzAlbum& b) {
            int cmp = 0;
            if (col == COL_A_TITLE) cmp = _stricmp(a.title.c_str(), b.title.c_str());
            else if (col == COL_A_ARTIST) cmp = _stricmp(a.artist.c_str(), b.artist.c_str());
            else if (col == COL_A_TRACKS) cmp = (a.tracks_count < b.tracks_count) ? -1 : (a.tracks_count > b.tracks_count ? 1 : 0);
            else if (col == COL_A_YEAR) cmp = (a.year < b.year) ? -1 : (a.year > b.year ? 1 : 0);
            else if (col == COL_A_HIRES) {
                bool a_hr = is_hires(a);
                bool b_hr = is_hires(b);
                cmp = (a_hr == b_hr) ? 0 : (a_hr ? 1 : -1);
            }
            else if (col == COL_A_QUALITY) {
                if (a.bit_depth != b.bit_depth) cmp = a.bit_depth < b.bit_depth ? -1 : 1;
                else cmp = (a.sampling_rate < b.sampling_rate) ? -1 : (a.sampling_rate > b.sampling_rate ? 1 : 0);
            }

            if (cmp == 0) return false;
            return asc ? (cmp < 0) : (cmp > 0);
        });
    }

    populate_albums(lv, dd->album_results);
    update_header_sort_icon(lv, dd->sort_column, dd->sort_ascending);

    wchar_t status[64];
    _snwprintf_s(status, 64, L"%d album(s) found.", (int)dd->album_results.size());
    SetDlgItemTextW(hwnd, IDC_STATUS_TEXT, status);
}

static void apply_filter_and_refresh(HWND hwnd, DialogData* dd) {
    if (!dd) return;
    if (dd->mode == MODE_TRACKS) {
        sort_and_refresh_tracks(hwnd, dd);
    } else {
        sort_and_refresh_albums(hwnd, dd);
    }
}

// ---- Art Viewer Dialog Proc ---------------------------------------------

struct ArtViewerState {
    std::string title;
    std::string cover_url;
    Gdiplus::Bitmap* bmp = nullptr;
};

static INT_PTR CALLBACK ArtViewerDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<ArtViewerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        state = reinterpret_cast<ArtViewerState*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        if (!state) return TRUE;

        std::wstring wtitle = L"Album Art";
        if (!state->title.empty()) {
            wtitle += L" - " + to_wide(state->title);
        }
        SetWindowTextW(hwnd, wtitle.c_str());

        if (!state->cover_url.empty()) {
            std::string url = state->cover_url;
            HWND hwnd_copy = hwnd;
            std::thread([url, hwnd_copy]() {
                try {
                    std::string data = g_qobuz_api.download_url(url.c_str());
                    if (!data.empty()) {
                        auto* heap_data = new std::string(std::move(data));
                        PostMessageW(hwnd_copy, WM_ART_LOADED, 0, (LPARAM)heap_data);
                    }
                } catch (...) {}
            }).detach();
        }
        return TRUE;
    }

    case WM_ART_LOADED: {
        auto* data = reinterpret_cast<std::string*>(lParam);
        if (state && data) {
            delete state->bmp;
            state->bmp = load_bitmap_from_memory(data->data(), data->size());
            delete data;
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    }

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis->CtlID == IDC_ART_VIEWER_IMAGE) {
            FillRect(dis->hDC, &dis->rcItem, (HBRUSH)GetStockObject(BLACK_BRUSH));
            if (state && state->bmp) {
                draw_bitmap_fitted(dis->hDC, state->bmp, dis->rcItem);
            }
            return TRUE;
        }
        break;
    }

    case WM_SIZE: {
        HWND hImg = GetDlgItem(hwnd, IDC_ART_VIEWER_IMAGE);
        if (hImg) {
            MoveWindow(hImg, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        }
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == IDCANCEL || LOWORD(wParam) == IDOK) {
            EndDialog(hwnd, 0);
            return TRUE;
        }
        break;
    }

    case WM_DESTROY: {
        if (state) {
            delete state->bmp;
            state->bmp = nullptr;
        }
        return 0;
    }
    }
    return FALSE;
}

static void show_album_art_window(HWND parent, const std::string& title, const std::string& cover_url) {
    if (cover_url.empty()) {
        popup_message::g_show("Cover art URL is not available.", "Qobuz", popup_message::icon_information);
        return;
    }
    ArtViewerState state;
    state.title = title;
    state.cover_url = cover_url;
    DialogBoxParamW(core_api::get_my_instance(),
                    MAKEINTRESOURCEW(IDD_QOBUZ_ART_VIEWER),
                    parent, ArtViewerDlgProc, (LPARAM)&state);
}

// ---- Album Details Dialog Proc -------------------------------------------

// Modular switch: set to 1 for HTML web view overlay, 0 to easily disable and use native edit control
#define ENABLE_HTML_DESCRIPTION 1

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

static inline POINT dlu_to_px(HWND hwnd, int dluX, int dluY) {
    RECT rc = { 0, 0, dluX, dluY };
    MapDialogRect(hwnd, &rc);
    return { rc.right, rc.bottom };
}

static std::string strip_html_tags(const std::string& input) {
    if (input.empty()) return "";

    std::string s;
    s.reserve(input.size());

    size_t i = 0;
    while (i < input.size()) {
        if (input[i] == '<') {
            size_t close = input.find('>', i);
            if (close == std::string::npos) break;

            std::string tag = input.substr(i + 1, close - i - 1);
            for (auto& c : tag) c = (char)tolower((unsigned char)c);

            if (tag == "br" || tag == "br/" || tag == "br /") {
                s += "\r\n";
            } else if (tag == "/p" || tag == "/div" || tag == "p" || tag == "div") {
                s += "\r\n";
            } else if (tag == "li") {
                s += "\r\n• ";
            }
            i = close + 1;
        } else if (input[i] == '&') {
            size_t semi = input.find(';', i);
            if (semi != std::string::npos && semi - i < 10) {
                std::string ent = input.substr(i, semi - i + 1);
                if (ent == "&amp;") s += '&';
                else if (ent == "&lt;") s += '<';
                else if (ent == "&gt;") s += '>';
                else if (ent == "&quot;") s += '"';
                else if (ent == "&#39;" || ent == "&apos;") s += '\'';
                else if (ent == "&nbsp;") s += ' ';
                else s += ' ';
                i = semi + 1;
            } else {
                s += input[i++];
            }
        } else {
            s += input[i++];
        }
    }

    std::string cleaned;
    cleaned.reserve(s.size());
    int consecutive_newlines = 0;
    for (char c : s) {
        if (c == '\r') continue;
        if (c == '\n') {
            consecutive_newlines++;
            if (consecutive_newlines <= 2) {
                cleaned += "\r\n";
            }
        } else {
            consecutive_newlines = 0;
            cleaned += c;
        }
    }

    size_t start = cleaned.find_first_not_of(" \t\r\n");
    if (start != std::string::npos) {
        cleaned = cleaned.substr(start);
    }
    size_t end = cleaned.find_last_not_of(" \t\r\n");
    if (end != std::string::npos) {
        cleaned = cleaned.substr(0, end + 1);
    }
    return cleaned;
}

struct AlbumDetailsDlgState {
    QobuzAlbumDetails* details = nullptr;
    Gdiplus::Bitmap*   art_bmp = nullptr;
#if ENABLE_HTML_DESCRIPTION
    HWND               hHtmlHost = nullptr;
#endif
};

static INT_PTR CALLBACK AlbumDetailsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<AlbumDetailsDlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        auto* details = reinterpret_cast<QobuzAlbumDetails*>(lParam);
        state = new AlbumDetailsDlgState();
        state->details = details;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        if (!details) return TRUE;

        // Title (with subtitle if present)
        std::string full_title = details->title;
        if (!details->subtitle.empty()) {
            full_title += " (" + details->subtitle + ")";
        }
        SetDlgItemTextW(hwnd, IDC_ALBUM_INFO_TITLE, to_wide(full_title).c_str());
        SetDlgItemTextW(hwnd, IDC_ALBUM_INFO_ARTIST, to_wide(details->artist).c_str());
        SetDlgItemTextW(hwnd, IDC_ALBUM_INFO_COMPOSER, to_wide(details->composer).c_str());
        SetDlgItemTextW(hwnd, IDC_ALBUM_INFO_LABEL, to_wide(details->label).c_str());
        SetDlgItemTextW(hwnd, IDC_ALBUM_INFO_GENRE, to_wide(details->genre).c_str());

        // Release date
        std::string rel_date = details->release_date_original;
        if (rel_date.empty()) rel_date = details->release_date_stream;
        if (!details->release_type.empty()) {
            if (!rel_date.empty()) rel_date += " ";
            rel_date += "[" + details->release_type + "]";
        }
        SetDlgItemTextW(hwnd, IDC_ALBUM_INFO_RELEASE_DATE, to_wide(rel_date).c_str());
        SetDlgItemTextW(hwnd, IDC_ALBUM_INFO_UPC, to_wide(details->upc).c_str());

        // Audio specifications
        std::wstring specs = format_quality_str(details->bit_depth, details->sampling_rate);
        if (details->channels > 0) {
            specs += (details->channels == 1) ? L" Mono" : (details->channels == 2) ? L" Stereo" : L" Multi-ch";
        }
        if (details->hires) {
            specs += L" (Hi-Res)";
        }
        SetDlgItemTextW(hwnd, IDC_ALBUM_INFO_SPECS, specs.c_str());

        // Volume (tracks / discs / duration)
        std::wstring vol_str = std::to_wstring(details->tracks_count) + L" track" + (details->tracks_count != 1 ? L"s" : L"");
        if (details->media_count > 1) {
            vol_str += L" (" + std::to_wstring(details->media_count) + L" discs)";
        }
        if (details->duration > 0.0) {
            vol_str += L" - " + format_duration(details->duration);
        }
        SetDlgItemTextW(hwnd, IDC_ALBUM_INFO_TRACKS_DUR, vol_str.c_str());

        SetDlgItemTextW(hwnd, IDC_ALBUM_INFO_COPYRIGHT, to_wide(details->copyright).c_str());

        // Credits / Performers
        std::string credits_str;
        for (const auto& ar : details->artists_roles) {
            credits_str += ar.name;
            if (!ar.roles.empty()) {
                credits_str += " (" + ar.roles + ")";
            }
            credits_str += "\r\n";
        }
        SetDlgItemTextW(hwnd, IDC_ALBUM_INFO_CREDITS, to_wide(credits_str).c_str());

        // Description / Review: Set clean plain text to standard EDIT control
        std::string plain_desc = strip_html_tags(details->description);
        SetDlgItemTextW(hwnd, IDC_ALBUM_INFO_DESC, to_wide(plain_desc).c_str());

#if ENABLE_HTML_DESCRIPTION
        // Dynamically create HTML host window overlaid over the EDIT control
        if (!details->description.empty()) {
            HWND hDesc = GetDlgItem(hwnd, IDC_ALBUM_INFO_DESC);
            if (hDesc) {
                RECT rc = {};
                GetWindowRect(hDesc, &rc);
                MapWindowPoints(NULL, hwnd, (LPPOINT)&rc, 2);

                RegisterHtmlHostWindowClass(core_api::get_my_instance());

                HWND hHtml = CreateWindowExW(
                    WS_EX_CLIENTEDGE,
                    L"QobuzHtmlHostWindow",
                    L"",
                    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                    rc.left, rc.top,
                    rc.right - rc.left, rc.bottom - rc.top,
                    hwnd,
                    (HMENU)(UINT_PTR)IDC_ALBUM_INFO_DESC_HTML,
                    core_api::get_my_instance(),
                    nullptr
                );

                if (hHtml) {
                    state->hHtmlHost = hHtml;
                    SendMessageW(hHtml, WM_HTMLVIEW_SETHTML, 0, (LPARAM)details->description.c_str());
                    ShowWindow(hDesc, SW_HIDE);
                }
            }
        }
#endif

        // Cover art download
        if (!details->cover_url.empty()) {
            std::string url = details->cover_url;
            HWND hwnd_copy = hwnd;
            std::thread([url, hwnd_copy]() {
                try {
                    std::string data = g_qobuz_api.download_url(url.c_str());
                    if (!data.empty()) {
                        auto* heap_data = new std::string(std::move(data));
                        PostMessageW(hwnd_copy, WM_ART_LOADED, 0, (LPARAM)heap_data);
                    }
                } catch (...) {}
            }).detach();
        } else {
            EnableWindow(GetDlgItem(hwnd, IDC_ALBUM_INFO_ART_BTN), FALSE);
        }

        if (details->url.empty()) {
            EnableWindow(GetDlgItem(hwnd, IDC_ALBUM_INFO_WEB_BTN), FALSE);
        }

        // Trigger initial layout
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        SendMessageW(hwnd, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));

        return TRUE;
    }

    case WM_ART_LOADED: {
        auto* data = reinterpret_cast<std::string*>(lParam);
        if (state && data) {
            delete state->art_bmp;
            state->art_bmp = load_bitmap_from_memory(data->data(), data->size());
            delete data;
            HWND hArt = GetDlgItem(hwnd, IDC_ALBUM_INFO_ART);
            if (hArt) InvalidateRect(hArt, NULL, TRUE);
        }
        return 0;
    }

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis->CtlID == IDC_ALBUM_INFO_ART) {
            FillRect(dis->hDC, &dis->rcItem, (HBRUSH)GetStockObject(BLACK_BRUSH));
            if (state && state->art_bmp) {
                draw_bitmap_fitted(dis->hDC, state->art_bmp, dis->rcItem);
            }
            return TRUE;
        }
        break;
    }

    case WM_DPICHANGED: {
        auto* prc = reinterpret_cast<RECT*>(lParam);
        if (prc) {
            SetWindowPos(hwnd, NULL,
                         prc->left, prc->top,
                         prc->right - prc->left, prc->bottom - prc->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }

    case WM_SIZE: {
        int cx = LOWORD(lParam);
        int cy = HIWORD(lParam);
        if (cx <= 0 || cy <= 0) return 0;

        // DLU conversion measurements using the dialog's current DPI font context
        POINT ptMargin   = dlu_to_px(hwnd, 10, 10);
        POINT ptRow      = dlu_to_px(hwnd, 55, 18);   // lblW: 55 DLU, rowPitch: 18 DLU
        POINT ptEditH    = dlu_to_px(hwnd, 0, 14);    // editH: 14 DLU
        POINT ptLblH     = dlu_to_px(hwnd, 0, 10);    // lblH: 10 DLU
        POINT ptBtnH     = dlu_to_px(hwnd, 0, 16);    // buttonH: 16 DLU
        POINT ptGap      = dlu_to_px(hwnd, 6, 6);
        POINT ptLeftW    = dlu_to_px(hwnd, 150, 0);   // base left pane width
        POINT ptBtnPlay  = dlu_to_px(hwnd, 85, 0);    // Add to Playlist button width
        POINT ptBtnWeb   = dlu_to_px(hwnd, 75, 0);    // Open in Web button width
        POINT ptBtnClose = dlu_to_px(hwnd, 55, 0);    // Close button width
        POINT ptBtnArt   = dlu_to_px(hwnd, 85, 0);    // Show Album Art button width
        POINT ptMinDesc  = dlu_to_px(hwnd, 0, 40);    // minimum review area height
        POINT ptMinCred  = dlu_to_px(hwnd, 0, 45);    // minimum credits area height

        int mX = ptMargin.x;
        int mY = ptMargin.y;
        int rowPitch = ptRow.y;
        int editH = ptEditH.y;
        int lblH = ptLblH.y;
        int lblW = ptRow.x;
        int btnH = ptBtnH.y;

        // Bottom action buttons Y
        int btnY = cy - mY - btnH;

        // ---- Left Pane (Album Art & Credits) ----
        int leftW = (std::max)(int(ptLeftW.x), int(cx * 32 / 100));

        // Album art: square at top-left, top-aligned (Y = mY)
        // Ensure credits area below has at least ptMinCred.y
        int maxArtH = btnY - ptGap.y - int(ptMinCred.y) - lblH - ptGap.y - btnH - ptGap.y - mY;
        int artSize = leftW;
        if (artSize > maxArtH) artSize = (std::max)(int(dlu_to_px(hwnd, 60, 0).x), maxArtH);

        HWND hArt = GetDlgItem(hwnd, IDC_ALBUM_INFO_ART);
        if (hArt) MoveWindow(hArt, mX, mY, artSize, artSize, TRUE);

        // "Show Album Art" button below the cover art
        int artBtnY = mY + artSize + ptGap.y;
        int artBtnW = (std::min)(int(ptBtnArt.x), leftW);
        int artBtnX = mX + (leftW - artBtnW) / 2;
        HWND hArtBtn = GetDlgItem(hwnd, IDC_ALBUM_INFO_ART_BTN);
        if (hArtBtn) MoveWindow(hArtBtn, artBtnX, artBtnY, artBtnW, btnH, TRUE);

        // Credits & Performers label & edit
        int credLblY = artBtnY + btnH + ptGap.y;
        HWND hCredLbl = GetDlgItem(hwnd, IDC_ALBUM_INFO_LBL_CREDITS);
        if (hCredLbl) MoveWindow(hCredLbl, mX, credLblY, leftW, lblH, TRUE);

        int credEditY = credLblY + lblH + (ptGap.y / 2);
        int credEditH = (btnY - ptGap.y) - credEditY;
        if (credEditH < int(ptMinCred.y)) credEditH = int(ptMinCred.y);
        HWND hCred = GetDlgItem(hwnd, IDC_ALBUM_INFO_CREDITS);
        if (hCred) MoveWindow(hCred, mX, credEditY, leftW, credEditH, TRUE);

        // ---- Right Pane (Properties & Description) ----
        int rightX = mX + leftW + (ptGap.x * 2);
        int rightW = cx - rightX - mX;
        if (rightW < int(dlu_to_px(hwnd, 150, 0).x)) rightW = int(dlu_to_px(hwnd, 150, 0).x);

        int editX = rightX + lblW + ptGap.x;
        int editW = rightW - lblW - ptGap.x;
        if (editW < int(dlu_to_px(hwnd, 50, 0).x)) editW = int(dlu_to_px(hwnd, 50, 0).x);

        struct PropRow {
            int lblId;
            int editId;
        };
        const PropRow rows[] = {
            { IDC_ALBUM_INFO_LBL_TITLE,        IDC_ALBUM_INFO_TITLE },
            { IDC_ALBUM_INFO_LBL_ARTIST,       IDC_ALBUM_INFO_ARTIST },
            { IDC_ALBUM_INFO_LBL_COMPOSER,     IDC_ALBUM_INFO_COMPOSER },
            { IDC_ALBUM_INFO_LBL_LABEL,        IDC_ALBUM_INFO_LABEL },
            { IDC_ALBUM_INFO_LBL_GENRE,        IDC_ALBUM_INFO_GENRE },
            { IDC_ALBUM_INFO_LBL_RELEASE_DATE, IDC_ALBUM_INFO_RELEASE_DATE },
            { IDC_ALBUM_INFO_LBL_UPC,          IDC_ALBUM_INFO_UPC },
            { IDC_ALBUM_INFO_LBL_SPECS,        IDC_ALBUM_INFO_SPECS },
            { IDC_ALBUM_INFO_LBL_TRACKS_DUR,   IDC_ALBUM_INFO_TRACKS_DUR },
            { IDC_ALBUM_INFO_LBL_COPYRIGHT,    IDC_ALBUM_INFO_COPYRIGHT }
        };

        int curY = mY;
        int lblOffset = (editH - lblH) / 2;

        for (const auto& r : rows) {
            HWND hLbl = GetDlgItem(hwnd, r.lblId);
            if (hLbl) MoveWindow(hLbl, rightX, curY + lblOffset, lblW, lblH, TRUE);

            HWND hEd = GetDlgItem(hwnd, r.editId);
            if (hEd) MoveWindow(hEd, editX, curY, editW, editH, TRUE);

            curY += rowPitch;
        }

        // Description / Review label & text
        curY += (ptGap.y / 2);
        HWND hDescLbl = GetDlgItem(hwnd, IDC_ALBUM_INFO_LBL_DESC);
        if (hDescLbl) MoveWindow(hDescLbl, rightX, curY, rightW, lblH, TRUE);

        int descY = curY + lblH + (ptGap.y / 2);
        int descH = (btnY - ptGap.y) - descY;
        if (descH < int(ptMinDesc.y)) descH = int(ptMinDesc.y);

        HWND hDesc = GetDlgItem(hwnd, IDC_ALBUM_INFO_DESC);
        if (hDesc) MoveWindow(hDesc, rightX, descY, rightW, descH, TRUE);

#if ENABLE_HTML_DESCRIPTION
        if (state && state->hHtmlHost) {
            MoveWindow(state->hHtmlHost, rightX, descY, rightW, descH, TRUE);
        }
#endif

        // Bottom action buttons
        HWND hBtnPlay = GetDlgItem(hwnd, IDC_ALBUM_INFO_PLAYLIST_BTN);
        if (hBtnPlay) MoveWindow(hBtnPlay, mX, btnY, ptBtnPlay.x, btnH, TRUE);

        HWND hBtnWeb = GetDlgItem(hwnd, IDC_ALBUM_INFO_WEB_BTN);
        if (hBtnWeb) MoveWindow(hBtnWeb, mX + ptBtnPlay.x + ptGap.x, btnY, ptBtnWeb.x, btnH, TRUE);

        HWND hBtnClose = GetDlgItem(hwnd, IDCANCEL);
        if (hBtnClose) MoveWindow(hBtnClose, cx - mX - ptBtnClose.x, btnY, ptBtnClose.x, btnH, TRUE);

        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_COMMAND: {
        int ctrl = LOWORD(wParam);

        if (ctrl == IDCANCEL || ctrl == IDOK) {
            EndDialog(hwnd, 0);
            return TRUE;
        }

        if (ctrl == IDC_ALBUM_INFO_ART_BTN && state && state->details && !state->details->cover_url.empty()) {
            show_album_art_window(hwnd, state->details->title, state->details->cover_url);
            return TRUE;
        }

        if (ctrl == IDC_ALBUM_INFO_WEB_BTN && state && state->details && !state->details->url.empty()) {
            ShellExecuteW(hwnd, L"open", to_wide(state->details->url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }

        if (ctrl == IDC_ALBUM_INFO_PLAYLIST_BTN && state && state->details) {
            std::string album_id = state->details->id;
            HWND hwnd_copy = hwnd;
            EnableWindow(GetDlgItem(hwnd, IDC_ALBUM_INFO_PLAYLIST_BTN), FALSE);
            std::thread([album_id, hwnd_copy]() {
                try {
                    abort_callback_impl abort_cb;
                    auto tracks = g_qobuz_api.get_album_tracks(album_id.c_str(), abort_cb);
                    fb2k::inMainThread([tracks]() {
                        add_tracks_to_playlist(tracks, false);
                    });
                } catch (...) {}
                if (IsWindow(hwnd_copy)) {
                    EnableWindow(GetDlgItem(hwnd_copy, IDC_ALBUM_INFO_PLAYLIST_BTN), TRUE);
                }
            }).detach();
            return TRUE;
        }
        break;
    }

    case WM_DESTROY: {
        if (state) {
            delete state->art_bmp;
            state->art_bmp = nullptr;
#if ENABLE_HTML_DESCRIPTION
            if (state->hHtmlHost) {
                DestroyWindow(state->hHtmlHost);
                state->hHtmlHost = nullptr;
            }
#endif
            delete state;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    }
    }
    return FALSE;
}

// ---- Track Details Dialog Proc -------------------------------------------

struct TrackDetailsDlgState {
    QobuzTrackDetails* details = nullptr;
};

static INT_PTR CALLBACK TrackDetailsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<TrackDetailsDlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        auto* details = reinterpret_cast<QobuzTrackDetails*>(lParam);
        state = new TrackDetailsDlgState();
        state->details = details;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        if (!details) return TRUE;

        std::string full_title = details->title;
        if (!details->version.empty()) {
            full_title += " (" + details->version + ")";
        }
        SetDlgItemTextW(hwnd, IDC_TRACK_INFO_TITLE, to_wide(full_title).c_str());
        SetDlgItemTextW(hwnd, IDC_TRACK_INFO_ARTIST, to_wide(details->performer).c_str());
        SetDlgItemTextW(hwnd, IDC_TRACK_INFO_COMPOSER, to_wide(details->composer).c_str());
        SetDlgItemTextW(hwnd, IDC_TRACK_INFO_WORK, to_wide(details->work).c_str());

        std::string alb_text = details->album_title;
        if (!details->album_artist.empty() && details->album_artist != details->performer) {
            alb_text += " (" + details->album_artist + ")";
        }
        SetDlgItemTextW(hwnd, IDC_TRACK_INFO_ALBUM, to_wide(alb_text).c_str());
        SetDlgItemTextW(hwnd, IDC_TRACK_INFO_LABEL, to_wide(details->label).c_str());
        SetDlgItemTextW(hwnd, IDC_TRACK_INFO_GENRE, to_wide(details->genre).c_str());
        SetDlgItemTextW(hwnd, IDC_TRACK_INFO_RELEASE_DATE, to_wide(details->release_date_original).c_str());
        SetDlgItemTextW(hwnd, IDC_TRACK_INFO_UPC, to_wide(details->upc).c_str());
        SetDlgItemTextW(hwnd, IDC_TRACK_INFO_ISRC, to_wide(details->isrc).c_str());

        std::wstring specs = format_quality_str(details->bit_depth, details->sampling_rate);
        if (details->channels > 0) {
            specs += (details->channels == 1) ? L" Mono" : (details->channels == 2) ? L" Stereo" : L" Multi-ch";
        }
        if (details->hires) specs += L" (Hi-Res)";
        SetDlgItemTextW(hwnd, IDC_TRACK_INFO_SPECS, specs.c_str());

        std::wstring trk_disc;
        if (details->track_number > 0) {
            trk_disc += L"Track " + std::to_wstring(details->track_number);
            if (details->total_tracks > 0) trk_disc += L"/" + std::to_wstring(details->total_tracks);
        }
        if (details->disc_number > 0) {
            if (!trk_disc.empty()) trk_disc += L", ";
            trk_disc += L"Disc " + std::to_wstring(details->disc_number);
            if (details->total_discs > 1) trk_disc += L"/" + std::to_wstring(details->total_discs);
        }
        if (details->duration > 0.0) {
            if (!trk_disc.empty()) trk_disc += L" - ";
            trk_disc += format_duration(details->duration);
        }
        SetDlgItemTextW(hwnd, IDC_TRACK_INFO_TRACK_DISC, trk_disc.c_str());

        if (details->has_rg) {
            wchar_t rg_buf[64];
            _snwprintf_s(rg_buf, 64, L"%.2f dB (Peak: %.4f)", details->rg_track_gain, details->rg_track_peak);
            SetDlgItemTextW(hwnd, IDC_TRACK_INFO_REPLAYGAIN, rg_buf);
        }

        SetDlgItemTextW(hwnd, IDC_TRACK_INFO_COPYRIGHT, to_wide(details->copyright).c_str());
        SetDlgItemTextW(hwnd, IDC_TRACK_INFO_PERFORMERS, to_wide(details->performers).c_str());

        if (details->cover_url.empty()) {
            EnableWindow(GetDlgItem(hwnd, IDC_TRACK_INFO_ART_BTN), FALSE);
        }
        if (details->url.empty()) {
            EnableWindow(GetDlgItem(hwnd, IDC_TRACK_INFO_WEB_BTN), FALSE);
        }
        return TRUE;
    }

    case WM_COMMAND: {
        int ctrl = LOWORD(wParam);
        if (ctrl == IDCANCEL || ctrl == IDOK) {
            EndDialog(hwnd, 0);
            return TRUE;
        }

        if (ctrl == IDC_TRACK_INFO_ART_BTN && state && state->details && !state->details->cover_url.empty()) {
            show_album_art_window(hwnd, state->details->album_title, state->details->cover_url);
            return TRUE;
        }

        if (ctrl == IDC_TRACK_INFO_WEB_BTN && state && state->details && !state->details->url.empty()) {
            ShellExecuteW(hwnd, L"open", to_wide(state->details->url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }

        if (ctrl == IDC_TRACK_INFO_PLAYLIST_BTN && state && state->details) {
            std::string tid = state->details->id;
            HWND hwnd_copy = hwnd;
            EnableWindow(GetDlgItem(hwnd, IDC_TRACK_INFO_PLAYLIST_BTN), FALSE);
            std::thread([tid, hwnd_copy]() {
                try {
                    abort_callback_impl abort_cb;
                    QobuzTrack tr = g_qobuz_api.get_track_info(tid.c_str(), abort_cb);
                    std::vector<QobuzTrack> single = { tr };
                    fb2k::inMainThread([single]() {
                        add_tracks_to_playlist(single, false);
                    });
                } catch (...) {}
                if (IsWindow(hwnd_copy)) {
                    EnableWindow(GetDlgItem(hwnd_copy, IDC_TRACK_INFO_PLAYLIST_BTN), TRUE);
                }
            }).detach();
            return TRUE;
        }
        break;
    }

    case WM_DESTROY: {
        delete state;
        return 0;
    }
    }
    return FALSE;
}

// ---- Dialog Proc ---------------------------------------------------------

static INT_PTR CALLBACK SearchDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DialogData* dd = reinterpret_cast<DialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        RegisterHtmlHostWindowClass();
        ensure_gdiplus();

        dd = new DialogData();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)dd);
        g_search_hwnd = hwnd;

        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
        InitCommonControlsEx(&icc);

        // Default to Albums mode
        CheckDlgButton(hwnd, IDC_TYPE_TRACKS, BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_TYPE_ALBUMS, BST_CHECKED);
        CheckDlgButton(hwnd, IDC_CHECK_HIRES, BST_UNCHECKED);

        HWND lv = GetDlgItem(hwnd, IDC_RESULTS_LIST);
        ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        setup_album_columns(lv);

        // Register with fb2k's modeless dialog manager so keyboard input works
        modeless_dialog_manager::g_add(hwnd);
        return TRUE;
    }

    case WM_DESTROY:
        modeless_dialog_manager::g_remove(hwnd);
        g_search_hwnd = nullptr;
        delete dd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;

    case WM_CLOSE:
        g_search_state.cancel = true;
        DestroyWindow(hwnd);
        return 0;

    case WM_COMMAND: {
        int ctrl = LOWORD(wParam);

        if (ctrl == IDC_CLOSE_BTN) {
            g_search_state.cancel = true;
            DestroyWindow(hwnd);
            return TRUE;
        }

        if (ctrl == IDC_CHECK_HIRES) {
            if (dd) {
                dd->filter_hires = (IsDlgButtonChecked(hwnd, IDC_CHECK_HIRES) == BST_CHECKED);
                apply_filter_and_refresh(hwnd, dd);
            }
            return TRUE;
        }

        if (ctrl == IDC_TYPE_TRACKS || ctrl == IDC_TYPE_ALBUMS) {
            SearchMode newMode = (ctrl == IDC_TYPE_TRACKS) ? MODE_TRACKS : MODE_ALBUMS;
            if (dd && newMode != dd->mode) {
                dd->mode = newMode;
                dd->sort_column = -1;
                dd->sort_ascending = true;
                HWND lv = GetDlgItem(hwnd, IDC_RESULTS_LIST);
                if (dd->mode == MODE_TRACKS) {
                    setup_track_columns(lv);
                    sort_and_refresh_tracks(hwnd, dd);
                } else {
                    setup_album_columns(lv);
                    sort_and_refresh_albums(hwnd, dd);
                }
            }
            return TRUE;
        }

        if (ctrl == IDC_SEARCH_BTN || ctrl == IDOK ||
            (ctrl == IDC_SEARCH_EDIT && HIWORD(wParam) == EN_CHANGE)) {

            if (ctrl == IDC_SEARCH_BTN || ctrl == IDOK) {
                // Kick off a background search
                g_search_state.cancel = true;  // cancel any previous search
                g_search_state.cancel = false; // reset for new one

                if (dd) {
                    dd->sort_column = -1;
                    dd->sort_ascending = true;
                }

                wchar_t wbuf[512] = {};
                GetDlgItemTextW(hwnd, IDC_SEARCH_EDIT, wbuf, 511);
                char query[512] = {};
                WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, query, 511, nullptr, nullptr);
                if (!query[0]) return TRUE;

                bool do_albums = (dd && dd->mode == MODE_ALBUMS);

                std::string q(query);
                HWND hwnd_copy = hwnd;
                SetDlgItemTextW(hwnd, IDC_STATUS_TEXT, L"Searching...");

                std::thread([q, do_albums, hwnd_copy]() {
                    try {
                        abort_callback_impl abort_cb;
                        // Poll the cancel flag roughly every 100ms isn't trivial with
                        // abort_callback_event here; just proceed and let it finish.

                        if (do_albums) {
                            auto results = g_qobuz_api.search_albums(q.c_str(), 1024, abort_cb);
                            auto* heap = new std::vector<QobuzAlbum>(std::move(results));
                            PostMessageW(hwnd_copy, WM_SEARCH_ALBUMS, 0, (LPARAM)heap);
                        } else {
                            auto results = g_qobuz_api.search_tracks(q.c_str(), 1024, abort_cb);
                            auto* heap = new std::vector<QobuzTrack>(std::move(results));
                            PostMessageW(hwnd_copy, WM_SEARCH_RESULTS, 0, (LPARAM)heap);
                        }
                    } catch (std::exception const& e) {
                        auto* msg = new std::string(e.what());
                        PostMessageW(hwnd_copy, WM_SEARCH_ERROR, 0, (LPARAM)msg);
                    }
                }).detach();
            }
            return TRUE;
        }

        if (ctrl == IDC_ADD_PLAYLIST || ctrl == IDC_PLAY_NOW) {
            if (!dd) return TRUE;
            HWND lv = GetDlgItem(hwnd, IDC_RESULTS_LIST);
            int sel = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
            if (sel < 0) return TRUE;

            bool play = (ctrl == IDC_PLAY_NOW);

            if (dd->mode == MODE_TRACKS && (size_t)sel < dd->track_results.size()) {
                std::vector<QobuzTrack> single = { dd->track_results[(size_t)sel] };
                fb2k::inMainThread([single, play]() {
                    add_tracks_to_playlist(single, play);
                });
            } else if (dd->mode == MODE_ALBUMS && (size_t)sel < dd->album_results.size()) {
                // Load album tracks in background then add
                std::string album_id = dd->album_results[(size_t)sel].id;
                HWND hwnd_copy = hwnd;
                SetDlgItemTextW(hwnd, IDC_STATUS_TEXT, L"Loading album...");
                std::thread([album_id, play, hwnd_copy]() {
                    try {
                        abort_callback_impl abort_cb;
                        auto tracks = g_qobuz_api.get_album_tracks(album_id.c_str(), abort_cb);
                        auto* heap = new std::vector<QobuzTrack>(std::move(tracks));
                        // Encode play flag in wParam
                        PostMessageW(hwnd_copy, WM_SEARCH_RESULTS + 0x10, play ? 1 : 0, (LPARAM)heap);
                    } catch (std::exception const& e) {
                        auto* msg = new std::string(e.what());
                        PostMessageW(hwnd_copy, WM_SEARCH_ERROR, 0, (LPARAM)msg);
                    }
                }).detach();
            }
            return TRUE;
        }

        if (ctrl == IDC_ALBUM_INFO) {
            if (!dd) return TRUE;
            HWND lv = GetDlgItem(hwnd, IDC_RESULTS_LIST);
            int sel = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
            if (sel < 0) return TRUE;

            std::string album_id;
            if (dd->mode == MODE_ALBUMS && (size_t)sel < dd->album_results.size()) {
                album_id = dd->album_results[(size_t)sel].id;
            } else if (dd->mode == MODE_TRACKS && (size_t)sel < dd->track_results.size()) {
                album_id = dd->track_results[(size_t)sel].album_id;
            }

            if (album_id.empty()) return TRUE;

            HWND hwnd_copy = hwnd;
            SetDlgItemTextW(hwnd, IDC_STATUS_TEXT, L"Loading album properties...");
            std::thread([album_id, hwnd_copy]() {
                try {
                    abort_callback_impl abort_cb;
                    auto details = g_qobuz_api.get_album_details(album_id.c_str(), abort_cb);
                    auto* heap = new QobuzAlbumDetails(std::move(details));
                    PostMessageW(hwnd_copy, WM_ALBUM_DETAILS, 0, (LPARAM)heap);
                } catch (std::exception const& e) {
                    auto* msg = new std::string(e.what());
                    PostMessageW(hwnd_copy, WM_SEARCH_ERROR, 0, (LPARAM)msg);
                }
            }).detach();
            return TRUE;
        }

        if (ctrl == IDC_TRACK_INFO) {
            if (!dd) return TRUE;
            HWND lv = GetDlgItem(hwnd, IDC_RESULTS_LIST);
            int sel = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
            if (sel < 0) return TRUE;

            std::string track_id;
            if (dd->mode == MODE_TRACKS && (size_t)sel < dd->track_results.size()) {
                track_id = dd->track_results[(size_t)sel].id;
            }

            if (track_id.empty()) return TRUE;

            HWND hwnd_copy = hwnd;
            SetDlgItemTextW(hwnd, IDC_STATUS_TEXT, L"Loading track properties...");
            std::thread([track_id, hwnd_copy]() {
                try {
                    abort_callback_impl abort_cb;
                    auto details = g_qobuz_api.get_track_details(track_id.c_str(), abort_cb);
                    auto* heap = new QobuzTrackDetails(std::move(details));
                    PostMessageW(hwnd_copy, WM_TRACK_DETAILS, 0, (LPARAM)heap);
                } catch (std::exception const& e) {
                    auto* msg = new std::string(e.what());
                    PostMessageW(hwnd_copy, WM_SEARCH_ERROR, 0, (LPARAM)msg);
                }
            }).detach();
            return TRUE;
        }

        if (ctrl == IDC_SHOW_ART) {
            if (!dd) return TRUE;
            HWND lv = GetDlgItem(hwnd, IDC_RESULTS_LIST);
            int sel = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
            if (sel < 0) return TRUE;

            std::string album_id;
            std::string fallback_title;
            if (dd->mode == MODE_ALBUMS && (size_t)sel < dd->album_results.size()) {
                album_id = dd->album_results[(size_t)sel].id;
                fallback_title = dd->album_results[(size_t)sel].title;
            } else if (dd->mode == MODE_TRACKS && (size_t)sel < dd->track_results.size()) {
                album_id = dd->track_results[(size_t)sel].album_id;
                fallback_title = dd->track_results[(size_t)sel].album;
            }

            if (album_id.empty()) return TRUE;

            HWND hwnd_copy = hwnd;
            SetDlgItemTextW(hwnd, IDC_STATUS_TEXT, L"Loading album art...");
            std::thread([album_id, fallback_title, hwnd_copy]() {
                try {
                    abort_callback_impl abort_cb;
                    auto details = g_qobuz_api.get_album_details(album_id.c_str(), abort_cb);
                    std::string art_url = details.cover_url;
                    std::string title = details.title.empty() ? fallback_title : details.title;
                    fb2k::inMainThread([hwnd_copy, title, art_url]() {
                        SetDlgItemTextW(hwnd_copy, IDC_STATUS_TEXT, L"");
                        show_album_art_window(hwnd_copy, title, art_url);
                    });
                } catch (std::exception const& e) {
                    auto* msg = new std::string(e.what());
                    PostMessageW(hwnd_copy, WM_SEARCH_ERROR, 0, (LPARAM)msg);
                }
            }).detach();
            return TRUE;
        }
        break;
    }

    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<NMHDR*>(lParam);
        if (nm->idFrom == IDC_RESULTS_LIST && nm->code == NM_DBLCLK) {
            // Double-click: add selected track to playlist and play
            if (!dd) return 0;
            HWND lv = GetDlgItem(hwnd, IDC_RESULTS_LIST);
            int sel = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
            if (sel < 0) return 0;

            if (dd->mode == MODE_TRACKS && (size_t)sel < dd->track_results.size()) {
                std::vector<QobuzTrack> single = { dd->track_results[(size_t)sel] };
                fb2k::inMainThread([single]() {
                    add_tracks_to_playlist(single, true);
                });
            } else if (dd->mode == MODE_ALBUMS && (size_t)sel < dd->album_results.size()) {
                // Expand album: load its tracks into the list
                std::string album_id = dd->album_results[(size_t)sel].id;
                HWND hwnd_copy = hwnd;
                CheckDlgButton(hwnd, IDC_TYPE_TRACKS, BST_CHECKED);
                CheckDlgButton(hwnd, IDC_TYPE_ALBUMS, BST_UNCHECKED);
                dd->mode = MODE_TRACKS;
                dd->sort_column = -1;
                dd->sort_ascending = true;
                setup_track_columns(GetDlgItem(hwnd, IDC_RESULTS_LIST));
                SetDlgItemTextW(hwnd, IDC_STATUS_TEXT, L"Loading album...");
                std::thread([album_id, hwnd_copy]() {
                    try {
                        abort_callback_impl abort_cb;
                        auto tracks = g_qobuz_api.get_album_tracks(album_id.c_str(), abort_cb);
                        auto* heap = new std::vector<QobuzTrack>(std::move(tracks));
                        PostMessageW(hwnd_copy, WM_SEARCH_RESULTS, 0, (LPARAM)heap);
                    } catch (std::exception const& e) {
                        auto* msg = new std::string(e.what());
                        PostMessageW(hwnd_copy, WM_SEARCH_ERROR, 0, (LPARAM)msg);
                    }
                }).detach();
            }
            return 0;
        }
        else if (nm->idFrom == IDC_RESULTS_LIST && nm->code == NM_RCLICK) {
            int sel = ListView_GetNextItem(nm->hwndFrom, -1, LVNI_SELECTED);
            if (sel != -1) {
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, IDC_ADD_PLAYLIST, L"Add to Playlist");
                AppendMenuW(hMenu, MF_STRING, IDC_PLAY_NOW, L"Play Now");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                if (dd && dd->mode == MODE_TRACKS) {
                    AppendMenuW(hMenu, MF_STRING, IDC_TRACK_INFO, L"Track Properties...");
                }
                AppendMenuW(hMenu, MF_STRING, IDC_ALBUM_INFO, L"Album Properties...");
                AppendMenuW(hMenu, MF_STRING, IDC_SHOW_ART, L"Show Album Art");
                POINT pt;
                GetCursorPos(&pt);
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            return 0;
        }
        else if (nm->idFrom == IDC_RESULTS_LIST && nm->code == LVN_COLUMNCLICK) {
            auto* nmlv = reinterpret_cast<LPNMLISTVIEW>(lParam);
            if (!dd) return 0;

            if (dd->sort_column == nmlv->iSubItem) {
                dd->sort_ascending = !dd->sort_ascending;
            } else {
                dd->sort_column = nmlv->iSubItem;
                dd->sort_ascending = true;
            }

            apply_filter_and_refresh(hwnd, dd);
            return 0;
        }
        break;
    }

    case WM_SEARCH_RESULTS: {
        auto* results = reinterpret_cast<std::vector<QobuzTrack>*>(lParam);
        if (dd && results) {
            dd->raw_track_results = std::move(*results);
            sort_and_refresh_tracks(hwnd, dd);
        }
        delete results;
        return 0;
    }

    case WM_SEARCH_RESULTS + 0x10: {
        // Album tracks loaded for add-to-playlist / play
        bool play = (wParam != 0);
        auto* tracks = reinterpret_cast<std::vector<QobuzTrack>*>(lParam);
        if (tracks) {
            std::vector<QobuzTrack> copy = std::move(*tracks);
            delete tracks;
            fb2k::inMainThread([copy, play]() {
                add_tracks_to_playlist(copy, play);
            });
        }
        wchar_t status[64];
        _snwprintf_s(status, 64, L"Added album to playlist.");
        SetDlgItemTextW(hwnd, IDC_STATUS_TEXT, status);
        return 0;
    }

    case WM_SEARCH_ALBUMS: {
        auto* results = reinterpret_cast<std::vector<QobuzAlbum>*>(lParam);
        if (dd && results) {
            dd->raw_album_results = std::move(*results);
            sort_and_refresh_albums(hwnd, dd);
        }
        delete results;
        return 0;
    }

    case WM_SEARCH_ERROR: {
        auto* msg = reinterpret_cast<std::string*>(lParam);
        if (msg) {
            std::wstring wmsg = to_wide(*msg);
            SetDlgItemTextW(hwnd, IDC_STATUS_TEXT, wmsg.c_str());
            delete msg;
        }
        return 0;
    }

    case WM_ALBUM_DETAILS: {
        auto* details = reinterpret_cast<QobuzAlbumDetails*>(lParam);
        if (details) {
            SetDlgItemTextW(hwnd, IDC_STATUS_TEXT, L"");
            INT_PTR ret = DialogBoxParamW(core_api::get_my_instance(),
                                          MAKEINTRESOURCEW(IDD_QOBUZ_ALBUM_INFO),
                                          hwnd, AlbumDetailsDlgProc, (LPARAM)details);
            if (ret == -1) {
                DWORD err = GetLastError();
                pfc::string8 msg;
                msg << "Failed to open Album Properties dialog. Error code: " << (unsigned)err;
                popup_message::g_show(msg.c_str(), "Qobuz", popup_message::icon_error);
            }
            delete details;
        }
        return 0;
    }

    case WM_TRACK_DETAILS: {
        auto* details = reinterpret_cast<QobuzTrackDetails*>(lParam);
        if (details) {
            SetDlgItemTextW(hwnd, IDC_STATUS_TEXT, L"");
            DialogBoxParamW(core_api::get_my_instance(),
                            MAKEINTRESOURCEW(IDD_QOBUZ_TRACK_INFO),
                            hwnd, TrackDetailsDlgProc, (LPARAM)details);
            delete details;
        }
        return 0;
    }

    } // switch
    return FALSE;
}

// ---- Main-menu command ---------------------------------------------------

static void open_search_dialog() {
    if (g_search_hwnd) {
        // Bring existing dialog to front instead of opening a second one
        SetForegroundWindow(g_search_hwnd);
        return;
    }

    HINSTANCE hInst = core_api::get_my_instance();
    HWND      wndParent = core_api::get_main_window();

    HWND hwnd = CreateDialogParamW(hInst, MAKEINTRESOURCEW(IDD_QOBUZ_SEARCH),
        wndParent, SearchDlgProc, 0);

    if (hwnd)
        ShowWindow(hwnd, SW_SHOW);
    else
        popup_message::g_show("Failed to create Qobuz search dialog.", "Qobuz", popup_message::icon_error);
}

// Main-menu group: under "View" → "Qobuz"
static mainmenu_group_popup_factory g_mainmenu_group(
    guid_mainmenu_group, mainmenu_groups::view,
    mainmenu_commands::sort_priority_dontcare, "Qobuz");

class qobuz_mainmenu_commands : public mainmenu_commands {
public:
    enum { cmd_search = 0, cmd_total };

    t_uint32 get_command_count() override { return cmd_total; }

    GUID get_command(t_uint32 idx) override {
        if (idx == cmd_search) return guid_cmd_search;
        uBugCheck();
    }

    void get_name(t_uint32 idx, pfc::string_base& out) override {
        if (idx == cmd_search) out = "Search...";
        else uBugCheck();
    }

    bool get_description(t_uint32 idx, pfc::string_base& out) override {
        if (idx == cmd_search) {
            out = "Search Qobuz for tracks and albums and add them to a playlist.";
            return true;
        }
        return false;
    }

    GUID get_parent() override { return guid_mainmenu_group; }

    void execute(t_uint32 idx, service_ptr_t<service_base>) override {
        if (idx == cmd_search) open_search_dialog();
        else uBugCheck();
    }
};

static mainmenu_commands_factory_t<qobuz_mainmenu_commands> g_qobuz_mainmenu_factory;

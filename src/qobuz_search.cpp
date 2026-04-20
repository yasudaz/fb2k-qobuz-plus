#include "stdafx.h"
#include "qobuz_api.h"
#include "resource.h"

#include <commctrl.h>
#include <windowsx.h>
#include <thread>
#include <atomic>
#include <mutex>

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
    for (auto& t : tracks) {
        pfc::string8 path;
        path << "qobuz://track/" << t.id.c_str();
        metadb_handle_ptr h;
        metadb::get()->handle_create(h, make_playable_location(path, 0));
        items.add_item(h);
    }

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

struct SearchState {
    HWND  hwnd        = nullptr;
    std::atomic<bool> cancel { false };
};

// Shared state for the single open search dialog (there can only be one)
static SearchState g_search_state;
static HWND        g_search_hwnd = nullptr;

// Column indices for track list
enum TrackCol { COL_TITLE=0, COL_ARTIST, COL_ALBUM, COL_DURATION, COL_QUALITY, COL_ID };
enum AlbumCol { COL_A_TITLE=0, COL_A_ARTIST, COL_A_TRACKS, COL_A_ID };

enum SearchMode { MODE_TRACKS, MODE_ALBUMS };

struct DialogData {
    SearchMode mode = MODE_TRACKS;
    std::vector<QobuzTrack>  track_results;
    std::vector<QobuzAlbum>  album_results;
    // For album-drill-down: current album_id being expanded
    std::string expand_album_id;
};

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
    add_col(L"Title",   200);
    add_col(L"Artist",  140);
    add_col(L"Album",   120);
    add_col(L"Duration", 55);
    add_col(L"Quality",  70);
    add_col(L"ID",        0);  // Hidden: used to retrieve track id
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
    add_col(L"Title",   240);
    add_col(L"Artist",  160);
    add_col(L"Tracks",   55);
    add_col(L"ID",         0);
}

// Utility: UTF-8 → wchar_t for ListView text
static std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
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

static std::wstring format_quality(const QobuzTrack& t) {
    wchar_t buf[32];
    _snwprintf_s(buf, 32, L"%dbit/%.0fkHz", t.bit_depth, t.sampling_rate);
    return buf;
}

static void populate_tracks(HWND lv, const std::vector<QobuzTrack>& tracks) {
    ListView_DeleteAllItems(lv);
    int row = 0;
    for (auto& t : tracks) {
        set_lv_item_text(lv, row, COL_TITLE,    to_wide(t.title));
        set_lv_item_text(lv, row, COL_ARTIST,   to_wide(t.artist));
        set_lv_item_text(lv, row, COL_ALBUM,    to_wide(t.album));
        set_lv_item_text(lv, row, COL_DURATION, format_duration(t.duration));
        set_lv_item_text(lv, row, COL_QUALITY,  format_quality(t));
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
    char narrow[64] = {};
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, narrow, sizeof(narrow), nullptr, nullptr);
    return narrow;
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
    char narrow[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, narrow, sizeof(narrow), nullptr, nullptr);
    return narrow;
}

// ---- Dialog Proc ---------------------------------------------------------

static INT_PTR CALLBACK SearchDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DialogData* dd = reinterpret_cast<DialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        dd = new DialogData();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)dd);
        g_search_hwnd = hwnd;

        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
        InitCommonControlsEx(&icc);

        // Default to Tracks mode
        CheckDlgButton(hwnd, IDC_TYPE_TRACKS, BST_CHECKED);
        CheckDlgButton(hwnd, IDC_TYPE_ALBUMS, BST_UNCHECKED);

        HWND lv = GetDlgItem(hwnd, IDC_RESULTS_LIST);
        ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        setup_track_columns(lv);

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

        if (ctrl == IDC_TYPE_TRACKS || ctrl == IDC_TYPE_ALBUMS) {
            SearchMode newMode = (ctrl == IDC_TYPE_TRACKS) ? MODE_TRACKS : MODE_ALBUMS;
            if (dd && newMode != dd->mode) {
                dd->mode = newMode;
                HWND lv = GetDlgItem(hwnd, IDC_RESULTS_LIST);
                if (dd->mode == MODE_TRACKS) {
                    setup_track_columns(lv);
                    populate_tracks(lv, dd->track_results);
                } else {
                    setup_album_columns(lv);
                    populate_albums(lv, dd->album_results);
                }
            }
            return TRUE;
        }

        if (ctrl == IDC_SEARCH_BTN ||
            (ctrl == IDC_SEARCH_EDIT && HIWORD(wParam) == EN_CHANGE)) {

            if (LOWORD(wParam) == IDC_SEARCH_BTN) {
                // Kick off a background search
                g_search_state.cancel = true;  // cancel any previous search
                g_search_state.cancel = false; // reset for new one

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
                            auto results = g_qobuz_api.search_albums(q.c_str(), 30, abort_cb);
                            auto* heap = new std::vector<QobuzAlbum>(std::move(results));
                            PostMessageW(hwnd_copy, WM_SEARCH_ALBUMS, 0, (LPARAM)heap);
                        } else {
                            auto results = g_qobuz_api.search_tracks(q.c_str(), 30, abort_cb);
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
        break;
    }

    case WM_SEARCH_RESULTS: {
        auto* results = reinterpret_cast<std::vector<QobuzTrack>*>(lParam);
        if (dd && results) {
            dd->track_results = std::move(*results);
            wchar_t status[64];
            _snwprintf_s(status, 64, L"%d track(s) found.", (int)dd->track_results.size());
            SetDlgItemTextW(hwnd, IDC_STATUS_TEXT, status);
            populate_tracks(GetDlgItem(hwnd, IDC_RESULTS_LIST), dd->track_results);
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
            dd->album_results = std::move(*results);
            wchar_t status[64];
            _snwprintf_s(status, 64, L"%d album(s) found.", (int)dd->album_results.size());
            SetDlgItemTextW(hwnd, IDC_STATUS_TEXT, status);
            populate_albums(GetDlgItem(hwnd, IDC_RESULTS_LIST), dd->album_results);
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

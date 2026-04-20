// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Carl Kittelberger <icedream@icedream.pw>

#include "stdafx.h"
#include "qobuz_api.h"
#include "resource.h"

// ---- Quality combo entries --------------------------------------------------

struct QualityEntry { const char* label; uint64_t format_id; };
static constexpr QualityEntry k_qualities[] = {
    { "Studio Master (24-bit, up to 192\xC2\xA0kHz FLAC)", 27 },
    { "Hi-Res (24-bit, up to 96\xC2\xA0kHz FLAC)",          7 },
    { "CD Quality (16-bit, 44.1\xC2\xA0kHz FLAC)",          6 },
    { "MP3 320\xC2\xA0kbps",                                  5 },
};
static constexpr int k_quality_count = (int)(sizeof(k_qualities) / sizeof(k_qualities[0]));

static int format_id_to_index(uint64_t id) {
    for (int i = 0; i < k_quality_count; ++i)
        if (k_qualities[i].format_id == id) return i;
    return 0; // default to Studio Master
}

// ---- Preferences page instance (one per dialog open) -----------------------

class QobuzPrefsInstance : public service_impl_t<preferences_page_instance> {
public:
    QobuzPrefsInstance(HWND parent, preferences_page_callback::ptr cb)
        : m_callback(cb)
    {
        m_wnd = CreateDialogParam(
            core_api::get_my_instance(),
            MAKEINTRESOURCE(IDD_QOBUZ_PREFS),
            parent,
            DialogProc,
            (LPARAM)this);
    }

    ~QobuzPrefsInstance() {
        if (m_wnd) DestroyWindow(m_wnd);
    }

    // ---- preferences_page_instance ------------------------------------------

    HWND get_wnd() override { return m_wnd; }

    t_uint32 get_state() override {
        t_uint32 state = preferences_state::resettable;
        if (m_changed) state |= preferences_state::changed;
        return state;
    }

    void apply() override {
        // Auth token
        int len = GetWindowTextLengthA(GetDlgItem(m_wnd, IDC_PREFS_AUTH_EDIT));
        std::string auth(len, '\0');
        GetDlgItemTextA(m_wnd, IDC_PREFS_AUTH_EDIT, &auth[0], len + 1);
        cfg_auth_token().set(auth.c_str());

        // Quality
        int sel = (int)SendDlgItemMessage(m_wnd, IDC_PREFS_QUAL_CMB, CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < k_quality_count)
            cfg_quality().set(k_qualities[sel].format_id);

        // App ID override
        len = GetWindowTextLengthA(GetDlgItem(m_wnd, IDC_PREFS_APPID_EDT));
        std::string app_id(len, '\0');
        GetDlgItemTextA(m_wnd, IDC_PREFS_APPID_EDT, &app_id[0], len + 1);
        cfg_app_id().set(app_id.c_str());

        // Secret override
        len = GetWindowTextLengthA(GetDlgItem(m_wnd, IDC_PREFS_SECRET_EDT));
        std::string secret(len, '\0');
        GetDlgItemTextA(m_wnd, IDC_PREFS_SECRET_EDT, &secret[0], len + 1);
        cfg_secret().set(secret.c_str());

        m_changed = false;
        m_callback->on_state_changed();
    }

    void reset() override {
        SetDlgItemTextA(m_wnd, IDC_PREFS_AUTH_EDIT,   "");
        SetDlgItemTextA(m_wnd, IDC_PREFS_APPID_EDT,   "");
        SetDlgItemTextA(m_wnd, IDC_PREFS_SECRET_EDT,  "");
        SendDlgItemMessage(m_wnd, IDC_PREFS_QUAL_CMB, CB_SETCURSEL, 0, 0); // Studio Master

        mark_changed();
    }

private:
    void on_init() {
        // Populate quality combo
        HWND hcmb = GetDlgItem(m_wnd, IDC_PREFS_QUAL_CMB);
        for (int i = 0; i < k_quality_count; ++i) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, k_qualities[i].label, -1, nullptr, 0);
            std::wstring wlabel(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, k_qualities[i].label, -1, &wlabel[0], wlen);
            SendMessageW(hcmb, CB_ADDSTRING, 0, (LPARAM)wlabel.c_str());
        }

        // Load current values
        pfc::string8 val;
        cfg_auth_token().get(val);
        SetDlgItemTextA(m_wnd, IDC_PREFS_AUTH_EDIT, val.c_str());

        SendDlgItemMessage(m_wnd, IDC_PREFS_QUAL_CMB, CB_SETCURSEL,
                           format_id_to_index(cfg_quality().get()), 0);

        cfg_app_id().get(val);
        SetDlgItemTextA(m_wnd, IDC_PREFS_APPID_EDT, val.c_str());

        cfg_secret().get(val);
        SetDlgItemTextA(m_wnd, IDC_PREFS_SECRET_EDT, val.c_str());

        m_changed = false;
    }

    void mark_changed() {
        m_changed = true;
        m_callback->on_state_changed();
    }

    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        QobuzPrefsInstance* self =
            reinterpret_cast<QobuzPrefsInstance*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

        switch (msg) {
        case WM_INITDIALOG:
            self = reinterpret_cast<QobuzPrefsInstance*>(lp);
            self->m_wnd = hwnd;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
            self->on_init();
            return TRUE;

        case WM_COMMAND:
            if (!self) return FALSE;
            switch (LOWORD(wp)) {
            case IDC_PREFS_AUTH_EDIT:
            case IDC_PREFS_APPID_EDT:
            case IDC_PREFS_SECRET_EDT:
                if (HIWORD(wp) == EN_CHANGE) self->mark_changed();
                break;
            case IDC_PREFS_QUAL_CMB:
                if (HIWORD(wp) == CBN_SELCHANGE) self->mark_changed();
                break;
            }
            return FALSE;
        }
        return FALSE;
    }

    HWND m_wnd = nullptr;
    preferences_page_callback::ptr m_callback;
    bool m_changed = false;
};

// ---- Preferences page entry -------------------------------------------------

class QobuzPrefsPage : public preferences_page_v3 {
public:
    const char* get_name() override { return "Qobuz"; }

    GUID get_guid() override {
        // {A4C27E31-8B5D-4F2A-9E6D-3C1B7A8F5E20}
        static constexpr GUID guid =
            { 0xa4c27e31, 0x8b5d, 0x4f2a, { 0x9e, 0x6d, 0x3c, 0x1b, 0x7a, 0x8f, 0x5e, 0x20 } };
        return guid;
    }

    GUID get_parent_guid() override { return preferences_page::guid_tools; }

    preferences_page_instance::ptr instantiate(HWND parent,
                                               preferences_page_callback::ptr cb) override
    {
        return fb2k::service_new<QobuzPrefsInstance>(parent, cb);
    }
};

static preferences_page_factory_t<QobuzPrefsPage> g_qobuz_prefs_page;

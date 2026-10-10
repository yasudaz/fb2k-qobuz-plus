// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Carl Kittelberger <icedream@icedream.pw>
// SPDX-FileCopyrightText: 2026 yasudaz <https://github.com/yasudaz>

#pragma once

#include <windows.h>
#include <ole2.h>
#include <mshtml.h>
#include <exdisp.h>
#include <mshtmhst.h>
#include <string>

#define WM_HTMLVIEW_SETHTML (WM_USER + 101)

class SimpleWebBrowserHost : public IOleClientSite,
                             public IOleInPlaceSite,
                             public IOleInPlaceFrame,
                             public IDocHostUIHandler {
public:
    SimpleWebBrowserHost(HWND hwnd) : m_hwnd(hwnd), m_refCount(1) {}
    virtual ~SimpleWebBrowserHost() { close(); }

    bool create() {
        IStorage* pStorage = nullptr;
        HRESULT hr = StgCreateStorageEx(nullptr,
            STGM_READWRITE | STGM_SHARE_EXCLUSIVE | STGM_DIRECT | STGM_CREATE,
            STGFMT_STORAGE, 0, nullptr, nullptr, IID_IStorage, (void**)&pStorage);
        if (FAILED(hr) || !pStorage) {
            return false;
        }

        hr = OleCreate(CLSID_WebBrowser, IID_IOleObject, OLERENDER_DRAW, 0, this, pStorage, (void**)&m_oleObject);
        pStorage->Release();
        if (FAILED(hr) || !m_oleObject) {
            return false;
        }

        m_oleObject->SetHostNames(L"QobuzHtmlHost", 0);
        OleSetContainedObject(m_oleObject, TRUE);

        RECT rc = {};
        GetClientRect(m_hwnd, &rc);

        if (FAILED(m_oleObject->DoVerb(OLEIVERB_INPLACEACTIVATE, NULL, this, 0, m_hwnd, &rc))) {
            return false;
        }

        if (FAILED(m_oleObject->QueryInterface(IID_IWebBrowser2, (void**)&m_webBrowser))) {
            return false;
        }

        VARIANT vUrl;
        VariantInit(&vUrl);
        vUrl.vt = VT_BSTR;
        vUrl.bstrVal = SysAllocString(L"about:blank");
        m_webBrowser->Navigate2(&vUrl, NULL, NULL, NULL, NULL);
        VariantClear(&vUrl);

        return true;
    }

    void set_html(const std::string& html_body) {
        if (!m_webBrowser) return;

        // Build HTML document with modern UTF-8 meta and clean typography
        std::string full_html =
            "<!DOCTYPE html><html><head><meta charset=\"utf-8\" /><meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\" />"
            "<style>"
            "body { font-family: 'Segoe UI', Meiryo, sans-serif; font-size: 13px; line-height: 1.5; color: #222; background: #fff; margin: 8px; }"
            "p { margin: 0 0 8px 0; }"
            "a { color: #0066cc; text-decoration: none; }"
            "a:hover { text-decoration: underline; }"
            "</style></head><body>"
            + html_body
            + "</body></html>";

        // Convert to BSTR
        int wlen = MultiByteToWideChar(CP_UTF8, 0, full_html.c_str(), -1, nullptr, 0);
        BSTR bstr = SysAllocStringLen(nullptr, wlen);
        MultiByteToWideChar(CP_UTF8, 0, full_html.c_str(), -1, bstr, wlen);

        IDispatch* pDisp = nullptr;
        if (SUCCEEDED(m_webBrowser->get_Document(&pDisp)) && pDisp) {
            IHTMLDocument2* pDoc = nullptr;
            if (SUCCEEDED(pDisp->QueryInterface(IID_IHTMLDocument2, (void**)&pDoc)) && pDoc) {
                SAFEARRAY* sa = SafeArrayCreateVector(VT_VARIANT, 0, 1);
                if (sa) {
                    VARIANT* v;
                    SafeArrayAccessData(sa, (void**)&v);
                    v->vt = VT_BSTR;
                    v->bstrVal = bstr;
                    SafeArrayUnaccessData(sa);

                    pDoc->write(sa);
                    pDoc->close();
                    SafeArrayDestroy(sa);
                } else {
                    SysFreeString(bstr);
                }
                pDoc->Release();
            } else {
                SysFreeString(bstr);
            }
            pDisp->Release();
        } else {
            SysFreeString(bstr);
        }
    }

    void resize(int cx, int cy) {
        if (!m_oleObject) return;
        RECT rc = { 0, 0, cx, cy };
        IOleInPlaceObject* ipo = nullptr;
        if (SUCCEEDED(m_oleObject->QueryInterface(IID_IOleInPlaceObject, (void**)&ipo)) && ipo) {
            ipo->SetObjectRects(&rc, &rc);
            ipo->Release();
        }
    }

    void close() {
        if (m_webBrowser) {
            m_webBrowser->Release();
            m_webBrowser = nullptr;
        }
        if (m_oleObject) {
            m_oleObject->Close(OLECLOSE_NOSAVE);
            m_oleObject->Release();
            m_oleObject = nullptr;
        }
    }

    // ---- IUnknown ----
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IOleClientSite)
            *ppv = static_cast<IOleClientSite*>(this);
        else if (riid == IID_IOleInPlaceSite)
            *ppv = static_cast<IOleInPlaceSite*>(this);
        else if (riid == IID_IOleInPlaceFrame)
            *ppv = static_cast<IOleInPlaceFrame*>(this);
        else if (riid == IID_IDocHostUIHandler)
            *ppv = static_cast<IDocHostUIHandler*>(this);
        else
            return E_NOINTERFACE;

        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }

    // ---- IOleClientSite ----
    HRESULT STDMETHODCALLTYPE SaveObject() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMoniker(DWORD, DWORD, IMoniker**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetContainer(IOleContainer**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShowObject() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnShowWindow(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RequestNewObjectLayout() override { return E_NOTIMPL; }

    // ---- IOleInPlaceSite ----
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* phwnd) override {
        if (!phwnd) return E_POINTER;
        *phwnd = m_hwnd;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CanInPlaceActivate() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnInPlaceActivate() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnUIActivate() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetWindowContext(IOleInPlaceFrame** ppFrame,
                                              IOleInPlaceUIWindow** ppDoc,
                                              LPRECT lprcPosRect,
                                              LPRECT lprcClipRect,
                                              LPOLEINPLACEFRAMEINFO lpFrameInfo) override {
        if (!ppFrame || !lprcPosRect || !lprcClipRect) return E_POINTER;
        *ppFrame = static_cast<IOleInPlaceFrame*>(this);
        (*ppFrame)->AddRef();
        if (ppDoc) *ppDoc = nullptr;

        GetClientRect(m_hwnd, lprcPosRect);
        GetClientRect(m_hwnd, lprcClipRect);

        if (lpFrameInfo) {
            lpFrameInfo->fMDIApp = FALSE;
            lpFrameInfo->hwndFrame = m_hwnd;
            lpFrameInfo->haccel = 0;
            lpFrameInfo->cAccelEntries = 0;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Scroll(SIZE) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OnUIDeactivate(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnInPlaceDeactivate() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DiscardUndoState() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DeactivateAndUndo() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OnPosRectChange(LPCRECT) override { return S_OK; }

    // ---- IOleInPlaceFrame ----
    HRESULT STDMETHODCALLTYPE GetBorder(LPRECT) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RequestBorderSpace(LPCBORDERWIDTHS) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetBorderSpace(LPCBORDERWIDTHS) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetActiveObject(IOleInPlaceActiveObject*, LPCOLESTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE InsertMenus(HMENU, LPOLEMENUGROUPWIDTHS) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetMenu(HMENU, HOLEMENU, HWND) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RemoveMenus(HMENU) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetStatusText(LPCOLESTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EnableModeless(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG, WORD) override { return E_NOTIMPL; }

    // ---- IDocHostUIHandler ----
    HRESULT STDMETHODCALLTYPE ShowContextMenu(DWORD, POINT*, IUnknown*, IDispatch*) override { return S_OK; } // Suppress default IE menu
    HRESULT STDMETHODCALLTYPE GetHostInfo(DOCHOSTUIINFO* pInfo) override {
        if (!pInfo) return E_POINTER;
        pInfo->cbSize = sizeof(DOCHOSTUIINFO);
        pInfo->dwFlags = DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_THEME | DOCHOSTUIFLAG_SCROLL_NO;
        pInfo->dwDoubleClick = DOCHOSTUIDBLCLK_DEFAULT;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ShowUI(DWORD, IOleInPlaceActiveObject*, IOleCommandTarget*, IOleInPlaceFrame*, IOleInPlaceUIWindow*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE HideUI() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE UpdateUI() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDocWindowActivate(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResizeBorder(LPCRECT, IOleInPlaceUIWindow*, BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG, const GUID*, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetOptionKeyPath(LPOLESTR*, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDropTarget(IDropTarget*, IDropTarget**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetExternal(IDispatch**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE TranslateUrl(DWORD, OLECHAR*, OLECHAR**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FilterDataObject(IDataObject*, IDataObject**) override { return E_NOTIMPL; }

private:
    HWND            m_hwnd = nullptr;
    ULONG           m_refCount = 1;
    IOleObject*     m_oleObject = nullptr;
    IWebBrowser2*   m_webBrowser = nullptr;
};

// Window procedure for custom child window that hosts the WebBrowser
inline LRESULT CALLBACK HtmlHostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* host = reinterpret_cast<SimpleWebBrowserHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        host = new SimpleWebBrowserHost(hwnd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)host);
        if (!host->create()) {
            host->Release();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    }
    case WM_SIZE: {
        if (host) {
            host->resize(LOWORD(lp), HIWORD(lp));
        }
        return 0;
    }
    case WM_HTMLVIEW_SETHTML: {
        if (host && lp) {
            const char* html = reinterpret_cast<const char*>(lp);
            host->set_html(html);
        }
        return 0;
    }
    case WM_DESTROY: {
        if (host) {
            host->close();
            host->Release();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

inline void RegisterHtmlHostWindowClass(HINSTANCE hInst = nullptr) {
    static bool registered = false;
    if (registered) return;

    OleInitialize(nullptr);

    WNDCLASSW wc = {};
    wc.style         = CS_GLOBALCLASS;
    wc.lpfnWndProc   = HtmlHostWndProc;
    wc.hInstance     = hInst ? hInst : GetModuleHandleW(nullptr);
    wc.lpszClassName = L"QobuzHtmlHostWindow";
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    registered = true;
}


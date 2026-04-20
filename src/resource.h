#pragma once

// ---- Win32 constants needed by llvm-rc (doesn't preprocess windows.h) ------
// Values match <winuser.h> / <winresrc.h>; #ifndef guards prevent conflicts
// when native rc.exe or windres provides them via SDK headers.

// Dialog styles
#ifndef DS_SETFONT
#define DS_SETFONT          0x0040
#endif
#ifndef DS_MODALFRAME
#define DS_MODALFRAME       0x0080
#endif
#ifndef DS_FIXEDSYS
#define DS_FIXEDSYS         0x0008
#endif

// Window styles
#ifndef WS_POPUP
#define WS_POPUP            0x80000000
#endif
#ifndef WS_CAPTION
#define WS_CAPTION          0x00C00000
#endif
#ifndef WS_SYSMENU
#define WS_SYSMENU          0x00080000
#endif
#ifndef WS_THICKFRAME
#define WS_THICKFRAME       0x00040000
#endif
#ifndef WS_TABSTOP
#define WS_TABSTOP          0x00010000
#endif
#ifndef WS_GROUP
#define WS_GROUP            0x00020000
#endif
#ifndef WS_BORDER
#define WS_BORDER           0x00800000
#endif
#ifndef WS_VSCROLL
#define WS_VSCROLL          0x00200000
#endif

// Edit control styles
#ifndef ES_AUTOHSCROLL
#define ES_AUTOHSCROLL      0x0080
#endif

// Button styles
#ifndef BS_AUTORADIOBUTTON
#define BS_AUTORADIOBUTTON  0x0009
#endif

// ListView styles
#ifndef LVS_REPORT
#define LVS_REPORT          0x0001
#endif
#ifndef LVS_SINGLESEL
#define LVS_SINGLESEL       0x0004
#endif
#ifndef LVS_SHOWSELALWAYS
#define LVS_SHOWSELALWAYS   0x0008
#endif

// ---- Control / dialog resource IDs -----------------------------------------
#define IDD_QOBUZ_SEARCH    1001
#define IDC_SEARCH_EDIT     1002
#define IDC_SEARCH_BTN      1003
#define IDC_RESULTS_LIST    1004
#define IDC_ADD_PLAYLIST    1005
#define IDC_PLAY_NOW        1006
#define IDC_CLOSE_BTN       1007
#define IDC_TYPE_TRACKS     1008
#define IDC_TYPE_ALBUMS     1009
#define IDC_STATUS_TEXT     1010

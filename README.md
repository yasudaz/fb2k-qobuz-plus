# foo_qobuz — Qobuz Streaming Plugin for foobar2000 (Windows)

Streams music from [Qobuz](https://www.qobuz.com/) directly inside foobar2000 on Windows with rich metadata, album art viewing, and hi-res audio support.

> [!IMPORTANT]
> **Platform Support**: This fork is designed and maintained exclusively for **Windows** (Windows 10 / 11, 32-bit and 64-bit). **Linux and macOS environments are NOT supported**.

---

## Changes in this fork

- **Dedicated Windows Platform Focus**: Deep integration with Win32, GDI+, WinHTTP, and Windows high-DPI scaling. Linux and macOS cross-compilation are discontinued.
- **Album Properties Dialog (`Album Properties...`)**:
  - Inspect comprehensive album metadata (Title, Artist, Composer, Label, Genre, Release Date, UPC, Audio Specs, Volume, Copyright).
  - Top-aligned square album art display that resizes proportionally with the window.
  - Dedicated "Show Album Art" launcher button.
  - Scrollable Credits & Performers list.
  - Rich HTML rendering for album Description & Editorial Reviews (with clean plain-text fallback).
  - One-click "Add to Playlist" (adds all album tracks) and "Open in Web" buttons.
- **Track Properties Dialog (`Track Properties...`)**:
  - Inspect full track metadata (Title, Artist, Composer, Work, Album, Label, Genre, Release Date, UPC, Audio Specs, Track/Disc numbers, ISRC, ReplayGain track gain & peak, Copyright, Performers & Credits).
  - Dedicated buttons to add to playlist, show album art, and open in web.
- **Standalone Album Art Viewer (`Show Album Art`)**:
  - View full-resolution cover art in a clean, resizable standalone window from context menus or property dialogs.
- **Enhanced Search Dialog (`View → Qobuz → Search…`)**:
  - **Album Search by default** with a single-click switch to Track Search.
  - Clickable column headers with interactive sorting (Title, Artist, Album/Tracks, Year/Date, Hi-Res, Quality).
  - Hi-Res audio filter checkbox to display only high-resolution results.
  - Pressing "Enter" in the search box immediately executes the search.
  - Configurable maximum search results (default 100, up to 10,000) in Preferences.
  - Fixed metadata preservation so track numbers, disc numbers, and titles appear immediately in playlists.
- **Per-Monitor High-DPI & Multi-Monitor Support**:
  - Dynamic dialog unit (`MapDialogRect`) calculations instead of hardcoded pixel sizes.
  - Flawless rendering on **4K high-DPI displays** (150%, 175%, 200% scaling) without squished text or overlapping controls.
  - Seamless layout re-adaptation when moving between 4K displays and Full HD (100% DPI) displays (`WM_DPICHANGED`).

---

## Features

- **Search Albums & Tracks** by keyword via **View → Qobuz → Search…**
- **Album & Track Properties** via right-click context menu or dialog buttons.
- **Standalone Cover Art Viewer** for detailed artwork inspection.
- **Playlist Integration** — add individual tracks or entire albums directly to active playlists or play immediately.
- **Full Metadata Support** — Title, Artist, Album, Composer, Work, Track/Disc number, Date, Genre, Label, ISRC, UPC, Copyright, Performers & Credits, and ReplayGain.
- **Hi-Res Audio Streaming** — streams up to 192 kHz / 24-bit FLAC depending on your subscription.
- **Direct URL Playback** — drag and drop or paste Qobuz links:
  - Track URLs: `https://open.qobuz.com/track/<id>`
  - Album URLs: `https://play.qobuz.com/album/<id>`, `https://open.qobuz.com/album/<id>`
  - Playlist URLs: `https://play.qobuz.com/playlist/<id>`, `https://open.qobuz.com/playlist/<id>`, `https://www.qobuz.com/<locale>/playlists/<slug>/<id>`
- Tracks are saved as `qobuz://track/<id>` URIs compatible with foobar2000 playlist files (`.fpl`, `.m3u8`).

---

## Requirements

- **Operating System**: Windows 10 or Windows 11 (32-bit or 64-bit)
  - *Note: Linux and macOS are NOT supported.*
- **foobar2000**: v1.6 or v2.0+ (32-bit or 64-bit)
- **Qobuz Account**: A valid Qobuz account (Studio or Sublime subscription required for lossless CD quality / Hi-Res FLAC).

---

## Configuration

Open **File → Preferences → Tools → Qobuz**.

### 1. Auth token (required)

The Auth Token identifies your Qobuz account:

1. Log in to the [Qobuz web player](https://play.qobuz.com/) in your browser.
2. Open developer tools (**F12**) → **Application** (or Storage) tab → **Local Storage** → `https://play.qobuz.com`.
3. Locate the key `user_auth_token` and copy its value.
4. Paste the token into the **Auth token** field in foobar2000 Preferences and click **Apply**.

*(Alternatively, if you use [qobuz-dl](https://github.com/Sei969/qobuz-dl), the token can be found in `~/.config/qobuz-dl/config.ini` under `auth_token`.)*

### 2. Audio quality

Select your preferred maximum streaming quality:

| Setting | Format ID | Details |
|---|---|---|
| **Studio Master** | 27 | 24-bit FLAC, up to 192 kHz |
| **Hi-Res** | 7 | 24-bit FLAC, up to 96 kHz |
| **CD Quality** | 6 | 16-bit FLAC, 44.1 kHz |
| **MP3 320 kbps** | 5 | 320 kbps MP3 |

### 3. Max search results

Set the maximum number of items returned per search query (default: 100, range: 10 to 10,000).

### 4. Advanced overrides (optional)

The plugin automatically extracts its `app_id` and signing secrets from the Qobuz web player at runtime. These fields can normally remain blank unless automatic scraping fails.

---

## Usage Guide

1. **Open Search Dialog**:
   - Go to **View → Qobuz → Search…**
   - Type in keywords (artist, album, track title) and press **Enter** or click **Search**.
2. **Browse Results**:
   - The dialog defaults to **Albums** mode. Switch to **Tracks** mode anytime.
   - Click column headers (e.g. Year, Quality, Title) to sort results.
   - Check **Hi-Res only** to filter results to 24-bit streams.
3. **Context Menu (Right-Click)**:
   - **Add to Playlist**: Appends selected track or album tracks to the active playlist.
   - **Play Now**: Adds and immediately begins playback.
   - **Album Properties...**: Opens the comprehensive album inspection dialog.
   - **Track Properties...** *(Tracks mode)*: Opens track properties.
   - **Show Album Art**: Opens the high-resolution cover art viewer.
4. **Drill-down into Albums**:
   - In Albums mode, double-clicking an album loads all of its tracks into the search list.

---

## Building on Windows

### Prerequisites

- Visual Studio 2022 (Community, Professional, or Build Tools) with the "Desktop development with C++" workload
- CMake ≥ 3.16
- Windows 10/11 SDK (version 10.0.19041.0 or newer)
- C++20 support (required by foobar2000 SDK)

The foobar2000 SDK is automatically downloaded and extracted into `.deps/` during CMake configuration.

### Build Commands

Open a command prompt (or PowerShell) in the repository root:

#### 64-bit (x64) Release Build:
```bat
cmake -B build-msvc -A x64
cmake --build build-msvc --config Release
:: Output: build-msvc/Release/foo_qobuz.dll
```

#### 32-bit (x86) Release Build:
```bat
cmake -B build-msvc-x86 -A Win32
cmake --build build-msvc-x86 --config Release
:: Output: build-msvc-x86/Release/foo_qobuz.dll
```

### Packaging `.fb2k-component`

To build the dual-architecture `.fb2k-component` distribution package:

```bat
cmake -DFOO_DLL="build-msvc/Release/foo_qobuz.dll" ^
      -DEXTRA_DLL="build-msvc-x86/Release/foo_qobuz.dll" ^
      -DOUTPUT="foo_qobuz.fb2k-component" ^
      -DARCH_X86=FALSE ^
      -P cmake/package_component.cmake
```

---

## Installation

1. In foobar2000, open **File → Preferences → Components**.
2. Click **Install…** and select `foo_qobuz.fb2k-component`.
3. Click **OK** and restart foobar2000.
4. Go to **File → Preferences → Tools → Qobuz** to enter your Auth Token.

---

## Acknowledgments

Special thanks to the original author, **Carl Kittelberger (icedream)**, for creating the original plugin foundation and sharing it with the open-source community.

---

## License

This project is licensed under the **GNU General Public License v3.0 or later** ([GPL-3.0-or-later](LICENSE)).

*Qobuz is a registered trademark of Xandrie SA. This project is an independent community development and is not affiliated with or endorsed by Qobuz.*

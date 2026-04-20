# foo_qobuz — Qobuz streaming plugin for foobar2000

Streams music from [Qobuz](https://www.qobuz.com/) directly inside foobar2000.

## Features

- **Search** tracks and albums by artist, title, or album name via **View → Qobuz → Search…**
- **Playlist integration** — search results can be added to a new playlist or played immediately
- **Full metadata** — title, artist, album, track/disc number, year, genre, ISRC, and more
- **Hi-res audio** — supports up to 192 kHz / 24-bit FLAC depending on your subscription
- **Album art** extraction
- **Preferences page** at *File → Preferences → Tools → Qobuz*
- Tracks are stored as `qobuz://track/<id>` URIs and work in saved playlists

## Requirements

- foobar2000 1.6 or later (32-bit or 64-bit build)
- A Qobuz account — Studio or Sublime subscription required for lossless / hi-res

## Configuration

Open **File → Preferences → Tools → Qobuz**.

### Auth token (required)

The auth token is the Qobuz user authentication token that identifies your account. To obtain it:

1. Log in to the [Qobuz web player](https://play.qobuz.com/) in your browser.
2. Open the browser developer tools (F12) → **Application** tab → **Local Storage** → `https://play.qobuz.com`.
3. Find the key `user_auth_token` and copy its value.

Alternatively, if you have [qobuz-dl](https://github.com/Sei969/qobuz-dl) configured, the token is stored in `~/.config/qobuz-dl/config.ini` under the key `auth_token`.

Paste the token into the **Auth token** field and click **Apply**.

### Audio quality

Choose the maximum stream quality. foobar2000 will receive the best available quality up to this setting:

| Setting | Format ID | Details |
|---|---|---|
| Studio Master | 27 | 24-bit FLAC, up to 192 kHz |
| Hi-Res | 7 | 24-bit FLAC, up to 96 kHz |
| CD Quality | 6 | 16-bit FLAC, 44.1 kHz |
| MP3 320 kbps | 5 | Lossy |

### Advanced overrides (optional)

The plugin automatically scrapes its `app_id` and signing secrets from the Qobuz web player at startup, so these fields are normally left empty. Fill them in only if the auto-fetch fails:

- **App ID** — Qobuz application identifier
- **Secret** — signing secret used to authenticate API requests

## Building

### Prerequisites

| Tool | Purpose |
|---|---|
| CMake ≥ 3.16 | Build system |
| [clang-cl](https://clang.llvm.org/) (Clang ≥ 15) | Cross-compiler (Linux only) |
| [xwin](https://github.com/Jake-Shadle/xwin) | Downloads MSVC headers and Windows SDK (Linux only) |

The foobar2000 SDK is downloaded automatically at configure time from [foobar2000.org/SDK](https://www.foobar2000.org/SDK). To use a local copy, pass `-DFB2K_SDK_DIR=/path/to/sdk`.

---

### Linux — cross-compile for Windows (64-bit and 32-bit)

The plugin uses [xwin](https://github.com/Jake-Shadle/xwin) to obtain the MSVC-compatible headers and Windows SDK libraries needed by the foobar2000 SDK (which requires the MSVC ABI). Headers are downloaded automatically on first configure if `XWIN_ACCEPT_LICENSE=ON` is set.

> **License notice** — by setting `XWIN_ACCEPT_LICENSE=ON` you accept the
> [Microsoft Visual C++ license](https://visualstudio.microsoft.com/license-terms/vs2022-clt-build-tools/).

#### 64-bit (x86-64)

```sh
cmake --preset linux-cross -DXWIN_ACCEPT_LICENSE=ON
cmake --build build-clang --target foo_qobuz
# Output: build-clang/foo_qobuz.dll
```

#### 32-bit (x86)

```sh
cmake --preset linux-cross-x86 -DXWIN_ACCEPT_LICENSE=ON
cmake --build build-clang-x86 --target foo_qobuz
# Output: build-clang-x86/foo_qobuz.dll
```

> **Note** — The 32-bit cross-toolchain targets `i686-pc-windows-msvc`.
> Ensure your Clang installation includes the `i686-pc-windows-msvc` target
> (on Debian/Ubuntu: `llvm` and `clang` packages include it).

---

### Windows — native build

No additional tools are required; CMake uses MSVC or clang-cl from your Visual Studio installation and resolves the Windows SDK automatically.

#### Visual Studio 2022 — 64-bit

```bat
cmake --preset windows-msvc
cmake --build build-msvc --target foo_qobuz
```

#### Visual Studio 2022 — 32-bit

```bat
cmake --preset windows-msvc-x86
cmake --build build-msvc-x86 --target foo_qobuz
```

#### clang-cl (Ninja) — 64-bit

Run from a **64-bit Visual Studio Developer Command Prompt**:

```bat
cmake --preset windows-clang-cl
cmake --build build-clang-win --target foo_qobuz
```

#### clang-cl (Ninja) — 32-bit

Run from a **32-bit (x86) Visual Studio Developer Command Prompt**:

```bat
cmake --preset windows-clang-cl-x86
cmake --build build-clang-win-x86 --target foo_qobuz
```

---

### Packaging a `.fb2k-component`

A `.fb2k-component` file is a ZIP archive with the 32-bit DLL at the root and the 64-bit DLL under `x64/`. Build both architectures first, then package them together.

#### Example (Linux, after building both arches)

```sh
cmake --preset linux-cross -DXWIN_ACCEPT_LICENSE=ON \
      -DFB2K_COMPONENT_EXTRA_DLL="$(pwd)/build-clang-x86/foo_qobuz.dll"
cmake --build build-clang --target foo_qobuz_component
# Output: build-clang/foo_qobuz.fb2k-component
```

Or invoke the packaging script directly without reconfiguring:

```sh
cmake \
  -DFOO_DLL="$(pwd)/build-clang/foo_qobuz.dll" \
  -DOUTPUT="$(pwd)/foo_qobuz.fb2k-component" \
  -DARCH_X86=FALSE \
  -DEXTRA_DLL="$(pwd)/build-clang-x86/foo_qobuz.dll" \
  -P cmake/package_component.cmake
```

The `FB2K_COMPONENT_EXTRA_DLL` variable is optional — if omitted, the package contains only the architecture you built.

## Installation

1. Open foobar2000 **Preferences → Components**.
2. Click **Install…** and select `foo_qobuz.fb2k-component`.
3. Click **OK** and restart foobar2000 when prompted.

## License

This project is provided as-is without warranty. Qobuz is a registered trademark of Xandrie SA.

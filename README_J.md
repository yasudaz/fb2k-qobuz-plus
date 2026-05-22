# foo_qobuz — foobar2000用 Qobuz ストリーミングプラグイン

foobar2000内で直接 [Qobuz](https://www.qobuz.com/) から音楽をストリーミング再生します。

## このフォークでの変更点

- Qobuz検索からインポートした際、プレイリストにトラック名が即座に表示されない問題を修正しました。
- 設定画面で検索結果の最大件数を設定可能にしました（デフォルト 100件、最大 10,000件）。
- 検索ダイアログで「Enter」キーを押して検索を実行できるようになりました。
- 検索ダイアログに「ハイレゾ/音質」列とソート機能を追加しました。フォントを Calibri 12pt に変更しました。プレイリスト追加時にトラックのプロパティ（トラック番号の保存バグ修正を含む）を含めるようにしました。

## 機能

- **検索**: **View → Qobuz → Search…** からアーティスト、タイトル、アルバム名でトラックやアルバムを検索できます。
- **プレイリスト連携**: 検索結果を新しいプレイリストに追加したり、即座に再生したりできます。
- **完全なメタデータ**: タイトル、アーティスト、アルバム、トラック/ディスク番号、年、ジャンル、ISRCなどを取得します。
- **ハイレゾ音源**: サブスクリプションに応じて最大 192 kHz / 24-bit の FLAC をサポートします。
- **アルバムアート**の取得
- **設定画面**: *File → Preferences → Tools → Qobuz* に用意されています。
- トラックは `qobuz://track/<id>` 形式の URI として保存され、保存されたプレイリストでも機能します。
- **ダイレクト URL サポート**: Qobuz の共有 URL を貼り付けたりドラッグしたりして、即座に再生できます。
  - トラック URL: `https://open.qobuz.com/track/<id>`
  - アルバム URL: `https://play.qobuz.com/album/<id>`, `https://open.qobuz.com/album/<id>`
  - プレイリスト URL: `https://play.qobuz.com/playlist/<id>`, `https://open.qobuz.com/playlist/<id>`, `https://www.qobuz.com/<locale>/playlists/<slug>/<id>`

## 必要条件

- foobar2000 1.6 以降 (32-bit または 64-bit 版)
- Qobuz アカウント — ロスレス/ハイレゾ再生には Studio または Sublime サブスクリプションが必要です。

## 設定

**File → Preferences → Tools → Qobuz** を開きます。

### 認証トークン (必須)

**注意:** 認証トークンの実装は Windows 向けに設計されています。Linux 環境ではそのままでは動作しません。

認証トークンは、アカウントを識別するための Qobuz ユーザー認証トークンです。取得方法：

1. ブラウザで Qobuz Web プレイヤー にログインします。
2. ブラウザの開発者ツール (F12) を開き、**Application** タブ → **Local Storage** → `https://play.qobuz.com` を選択します。
3. `user_auth_token` というキーを探し、その値をコピーします。

または、qobuz-dl を設定している場合、トークンは `~/.config/qobuz-dl/config.ini` の `auth_token` キーに保存されています。

トークンを **Auth token** フィールドに貼り付け、**Apply** をクリックします。

### 音質設定

最大ストリーミング品質を選択します。foobar2000 は、この設定を上限として利用可能な最高品質を受信します。

| 設定項目 | フォーマット ID | 詳細 |
|---|---|---|
| Studio Master | 27 | 24-bit FLAC, 最大 192 kHz |
| Hi-Res | 7 | 24-bit FLAC, 最大 96 kHz |
| CD Quality | 6 | 16-bit FLAC, 44.1 kHz |
| MP3 320 kbps | 5 | 有損圧縮 (Lossy) |

### 高度なオーバーライド (オプション)

プラグインは起動時に Qobuz Web プレイヤーから `app_id` と署名用シークレットを自動的に取得するため、通常これらのフィールドは空のままにします。自動取得に失敗する場合のみ、以下を入力してください。

- **App ID** — Qobuz アプリケーション識別子
- **Secret** — API リクエストの認証に使用される署名用シークレット

## ビルド方法

### 準備するもの

| ツール | 用途 |
|---|---|
| CMake ≥ 3.16 | ビルドシステム |

foobar2000 SDK は、構成時に foobar2000.org/SDK から自動的にダウンロードされます。ローカルのコピーを使用する場合は、`-DFB2K_SDK_DIR=/path/to/sdk` を渡してください。

---

### Linux — Windows 用クロスコンパイル (64-bit および 32-bit)

このプラグインは、foobar2000 SDK (MSVC ABI が必要) に必要な MSVC 互換ヘッダーと Windows SDK ライブラリを取得するために xwin を使用します。`XWIN_ACCEPT_LICENSE=ON` を設定すると、最初の構成時にヘッダーが自動的にダウンロードされます。

> **ライセンスに関する通知** — `XWIN_ACCEPT_LICENSE=ON` を設定することで、Microsoft Visual C++ ライセンスに同意したことになります。

#### 64-bit (x86-64)

```sh
cmake --build build-clang --target foo_qobuz
# 出力: build-clang/foo_qobuz.dll
```

#### 32-bit (x86)

```sh
cmake --build build-clang-x86 --target foo_qobuz
# 出力: build-clang-x86/foo_qobuz.dll
```

> **注意** — 32-bit クロスツールチェーンは `i686-pc-windows-msvc` をターゲットにします。Clang のインストールに `i686-pc-windows-msvc` ターゲットが含まれていることを確認してください (Debian/Ubuntu の場合: `llvm` と `clang` パッケージに含まれています)。

---

### Windows — ネイティブビルド

追加のツールは不要です。CMake はインストール済みの Visual Studio から MSVC または clang-cl を使用し、Windows SDK を自動的に解決します。

#### Visual Studio 2022 — 64-bit

```bat
cmake --preset windows-msvc
cmake --build build-msvc --target foo_qobuz --config Release
```

#### Visual Studio 2022 — 32-bit

```bat
cmake --preset windows-msvc-x86
cmake --build build-msvc-x86 --target foo_qobuz --config Release
```

#### clang-cl (Ninja) — 64-bit

**64-bit Visual Studio Developer Command Prompt** から実行してください。

```bat
cmake --preset windows-clang-cl
cmake --build build-clang-win --target foo_qobuz --config Release
```

#### clang-cl (Ninja) — 32-bit

**32-bit (x86) Visual Studio Developer Command Prompt** から実行してください。

```bat
cmake --preset windows-clang-cl-x86
cmake --build build-clang-win-x86 --target foo_qobuz --config Release
```

---

### `.fb2k-component` のパッケージング

`.fb2k-component` ファイルは、ルートに 32-bit DLL、`x64/` 下に 64-bit DLL を含む ZIP アーカイブです。まず両方のアーキテクチャをビルドし、それから一緒にパッケージ化します。

再構成せずにパッケージングスクリプトを直接呼び出す場合：

```sh
cmake \
  -DFOO_DLL="$(pwd)/build-clang/foo_qobuz.dll" \
  -DOUTPUT="$(pwd)/foo_qobuz.fb2k-component" \
  -DARCH_X86=FALSE \
  -DEXTRA_DLL="$(pwd)/build-clang-x86/foo_qobuz.dll" \
  -P cmake/package_component.cmake
```

`FB2K_COMPONENT_EXTRA_DLL` 変数はオプションです。省略した場合、ビルドしたアーキテクチャのみが含まれます。

## インストール方法

1. foobar2000 の **Preferences → Components** を開きます。
2. **Install…** をクリックし、`foo_qobuz.fb2k-component` を選択します。
3. **OK** をクリックし、促されたら foobar2000 を再起動します。

## 謝辞

この素晴らしいプラグインを作成し、オープンソースコミュニティに共有してくれた原作者の Carl Kittelberger (icedream) 氏に深く感謝します。このフォークは、氏の優れた基盤の上に構築されています。

## ライセンス

このプロジェクトは **GNU General Public License v3.0 以降** の下でライセンスされています (SPDX: `GPL-3.0-or-later`)。全文は LICENSE を参照してください。

Qobuz は Xandrie SA の登録商標です。このプロジェクトは Qobuz と提携しておらず、また Qobuz によって承認されているものでもありません。

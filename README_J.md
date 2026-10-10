# foo_qobuz — foobar2000用 Qobuz ストリーミングプラグイン (Windows専用)

[Qobuz](https://www.qobuz.com/) の音楽を foobar2000 上で直接高音質ストリーミング再生できるプラグインです。豊富なメタデータ表示、アルバムアートの閲覧、最大 192 kHz / 24-bit のハイレゾ音源再生に対応しています。

> [!IMPORTANT]
> **対応プラットフォームについて**: 本フォーク（`foo_qobuz-plus`）は **Windows 専用**（Windows 10 / 11、32-bit および 64-bit）として設計・開発されています。**Linux 環境および macOS 環境はサポートされません（非対応）**。

---

## 本フォークでの主な追加機能・変更点

- **Windows 環境への特化**: Win32 ネイティブ GUI、GDI+、WinHTTP、高DPI スケーリング機能を深く統合。Linux / macOS のクロスコンパイル環境への依存を撤廃しました。
- **アルバム詳細プロパティ画面 (`Album Properties...`)**:
  - アルバムの全メタデータ（タイトル、アーティスト、作曲者、レーベル、ジャンル、リリース日、UPC、オーディオ仕様、収録曲数・総時間、著作権表記）を一覧表示。
  - ウィンドウサイズと連動して拡大縮小する、上詰めの正方形アルバムアート表示。
  - 単独アートビューアを開く「Show Album Art」ボタン。
  - スクロール可能な「Credits & Performers（クレジット・演奏者一覧）」表示。
  - レビュー・解説文（Description / Review）のリッチな HTML レンダリング表示（クリーンなテキストフォールバック機能付き）。
  - アルバムの全曲をワンクリックでプレイリストに追加できる「Add to Playlist」ボタン、Qobuz 公式アルバムページを開く「Open in Web」ボタン。
- **トラック詳細プロパティ画面 (`Track Properties...`)**:
  - トラックの全メタデータ（タイトル、アーティスト、作曲者、楽曲名(Work)、アルバム情報、レーベル、ジャンル、リリース日、UPC、仕様、トラック/ディスク番号、ISRC、ReplayGain ゲイン・ピーク値、著作権表記、演奏者・クレジット）を詳細表示。
  - プレイリスト追加、アルバムアート表示、Web 表示ボタンを完備。
- **単独アルバムアートビューア (`Show Album Art`)**:
  - 右クリックメニューや各プロパティ画面から起動可能。リサイズ可能な単独ウィンドウで高解像度のアートワークをじっくり鑑賞できます。
- **検索ダイアログの大幅刷新 (`View → Qobuz → Search…`)**:
  - **起動時のデフォルトを「アルバム検索（Albums）」に変更**（トラック検索への切り替えもワンクリック）。
  - カラムヘッダー（Title, Artist, Album/Tracks, Year/Date, Hi-Res, Quality）のクリックによる並び替え（ソート機能）。
  - 24-bit ハイレゾ音源のみを絞り込む「Hi-Res only」チェックボックス。
  - 検索ボックスでの **Enter キー入力による即時検索実行**。
  - 最大検索結果件数の設定（Preferences で 100〜10,000 件まで調整可能）。
  - 検索結果からプレイリストに追加した際、トラック番号やディスク番号、タイトルなどのメタデータが即座に正しく反映されるよう修正。
- **マルチモニター・4K 高DPI 拡大表示への完全対応**:
  - ピクセル固定値を廃止し、OS のダイアログ単位（`MapDialogRect`）による動的寸法算出を導入。
  - **4K 高解像度ディスプレイ（150%、175%、200%拡大など）** においても、文字の潰れ・見切れ・コントロールの重なりが一切発生しません。
  - 4K ディスプレイ ↔ フルHD ディスプレイ（100%）間をウィンドウ移動させた場合でも、リアルタイムに解像度を自動追従して再配置されます（`WM_DPICHANGED` 対応）。

---

## 主な機能

- **検索機能**: **View → Qobuz → Search…** からアーティスト、アルバム、トラックをキーワード検索
- **プロパティ確認**: 検索結果からアルバムやトラックの詳細情報をダイアログで確認
- **単独アートビューア**: カバーアートのみを大きなウィンドウで閲覧
- **プレイリスト連携**: 検索結果から選択した楽曲やアルバム全体をアクティブなプレイリストに追加、または即座に再生
- **充実したメタデータ**: タイトル、アーティスト、アルバム、作曲者、楽曲、トラック/ディスク番号、年代、ジャンル、レーベル、ISRC、UPC、著作権、クレジット、ReplayGain をサポート
- **ハイレゾストリーミング**: ご契約の Qobuz サブスクリプションプランに応じて、最大 192 kHz / 24-bit FLAC をロスレス再生
- **Qobuz URL の直接再生**: Web ブラウザの Qobuz 共有 URL を foobar2000 にドラッグ＆ドロップまたは貼り付けで即座に再生
  - トラック URL: `https://open.qobuz.com/track/<id>`
  - アルバム URL: `https://play.qobuz.com/album/<id>`, `https://open.qobuz.com/album/<id>`
  - プレイリスト URL: `https://play.qobuz.com/playlist/<id>`, `https://open.qobuz.com/playlist/<id>`, `https://www.qobuz.com/<locale>/playlists/<slug>/<id>`
- トラックは `qobuz://track/<id>` 形式の URI として扱われ、foobar2000 のプレイリストファイル（`.fpl`, `.m3u8`）に保存可能

---

## 動作要件

- **OS**: Windows 10 または Windows 11（32-bit または 64-bit）
  - *※ Linux 環境および macOS 環境はサポートされません。*
- **foobar2000**: v1.6 または v2.0 以上（32-bit / 64-bit）
- **Qobuz アカウント**: 有効な Qobuz アカウント（CD音質・ハイレゾFLAC 再生には Studio または Sublime プランの契約が必要です）

---

## 設定方法

foobar2000 のメニューから **File → Preferences → Tools → Qobuz** を開きます。

### 1. Auth token（認証トークン・必須）

ご自身の Qobuz アカウントを識別するための認証トークンを設定します：

1. Web ブラウザで [Qobuz Web Player](https://play.qobuz.com/) にログインします。
2. キーボードの **F12** キーを押して開発者ツールを開きます。
3. **Application**（または「ストレージ」）タブ → **Local Storage** → `https://play.qobuz.com` を選択します。
4. キー一覧から `user_auth_token` を探し、その値をコピーします。
5. foobar2000 の設定画面にある **Auth token** 欄に貼り付け、**Apply** をクリックします。

*(※ [qobuz-dl](https://github.com/Sei969/qobuz-dl) を利用している場合は、`~/.config/qobuz-dl/config.ini` 内の `auth_token` からも取得できます)*

### 2. Audio quality（再生音質）

ストリーミング再生の最大音質を選択します：

| 設定項目 | フォーマットID | 仕様 |
|---|---|---|
| **Studio Master** | 27 | 24-bit FLAC、最大 192 kHz |
| **Hi-Res** | 7 | 24-bit FLAC、最大 96 kHz |
| **CD Quality** | 6 | 16-bit FLAC、44.1 kHz |
| **MP3 320 kbps** | 5 | 320 kbps MP3 |

### 3. Max search results（最大検索件数）

検索クエリごとに取得する最大件数を設定します（初期値: 100件、設定可能範囲: 10〜10,000件）。

### 4. Advanced overrides（詳細設定・通常は空欄で可）

プラグイン起動時に Qobuz Web Player から最新の `app_id` と署名用シークレットキーを自動取得します。通常は空欄のままで動作します。自動取得に失敗した場合のみ手動入力してください。

---

## 使い方ガイド

1. **検索ダイアログを開く**:
   - メニューの **View → Qobuz → Search…** を選択します。
   - 検索ワード（アーティスト名、アルバム名、曲名）を入力し、**Enter** キーを押すか「Search」ボタンをクリックします。
2. **検索結果の確認**:
   - 初期状態では「Albums（アルバム検索）」が選択されています。「Tracks」ラジオボタンでトラック検索に切り替えることも可能です。
   - カラムヘッダー（Release 年、音質、タイトル等）をクリックして昇順・降順に並び替えできます。
   - 「Hi-Res only」にチェックを入れると、ハイレゾ音源のみに絞り込まれます。
3. **右クリックコンテキストメニュー**:
   - **Add to Playlist**: 選択したトラックまたはアルバム内の全曲を現在のプレイリストに追加します。
   - **Play Now**: プレイリストに追加して即座に再生を開始します。
   - **Album Properties...**: アルバムの詳細情報画面を開きます。
   - **Track Properties...** *(Tracks 検索時)*: トラックの詳細情報画面を開きます。
   - **Show Album Art**: 高解像度のアルバムアート単体ビューアを開きます。
4. **アルバムからのドリルダウン**:
   - Albums モードでアルバム項目をダブルクリックすると、そのアルバムの収録曲一覧がトラックリストとして展開表示されます。

---

## Windows 上でのビルド方法

### 必要な環境

- Visual Studio 2022（「C++ によるデスクトップ開発」ワークロードをインストール）
- CMake ≥ 3.16
- Windows 10/11 SDK（10.0.19041.0 以降）
- C++20 対応コンパイラ（foobar2000 SDK の要求仕様）

foobar2000 SDK は CMake の構成時に自動的にダウンロードされ、`.deps/` フォルダに展開されます。

### ビルドコマンド

リポジトリのルートディレクトリでコマンドプロンプトまたは PowerShell を開きます：

#### 64-bit (x64) Release ビルド:
```bat
cmake -B build-msvc -A x64
cmake --build build-msvc --config Release
:: 出力ファイル: build-msvc/Release/foo_qobuz.dll
```

#### 32-bit (x86) Release ビルド:
```bat
cmake -B build-msvc-x86 -A Win32
cmake --build build-msvc-x86 --config Release
:: 出力ファイル: build-msvc-x86/Release/foo_qobuz.dll
```

### コンポーネントパッケージ (`.fb2k-component`) の作成

32-bit と 64-bit の両方をビルドした後、以下のコマンドで統合パッケージを作成できます：

```bat
cmake -DFOO_DLL="build-msvc/Release/foo_qobuz.dll" ^
      -DEXTRA_DLL="build-msvc-x86/Release/foo_qobuz.dll" ^
      -DOUTPUT="foo_qobuz.fb2k-component" ^
      -DARCH_X86=FALSE ^
      -P cmake/package_component.cmake
```

---

## インストール手順

1. foobar2000 のメニューから **File → Preferences → Components** を開きます。
2. **Install…** ボタンをクリックし、作成した `foo_qobuz.fb2k-component` を選択します。
3. **OK** をクリックし、メッセージに従って foobar2000 を再起動します。
4. 再起動後、**File → Preferences → Tools → Qobuz** を開き、Auth Token を設定してください。

---

## 謝辞

オリジナルのプラグインを開発し、オープンソースとして公開してくださった原作者 **Carl Kittelberger (icedream)** 氏に心より感謝申し上げます。本フォークはその優れた基盤の上に成り立っています。

---

## ライセンス

本プロジェクトは **GNU General Public License v3.0 or later** ([GPL-3.0-or-later](LICENSE)) の下で公開されています。

*Qobuz は Xandrie SA の登録商標です。本プロジェクトは有志による独立した開発であり、Qobuz 公式とは一切提携・推奨関係にありません。*


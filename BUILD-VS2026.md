# Visual Studio 2026 でのビルド

## ソリューションを開く

`Generate-VS2026.cmd` を実行し、生成された `build/vs2026/yabause.slnx` を
Visual Studio 2026 で開いてください。構成は `Release | x64` または `Debug | x64` を選択します。
スタートアッププロジェクトは `yabause-qt` です。

「ビルド」→「ソリューションのビルド」で、次の場所に実行ファイルが作成されます。

```text
build/vs2026/src/qt/Release/yabause.exe
build/vs2026/src/qt/Debug/yabause.exe
```

Qt・SDL2 の DLL と Qt プラグインはビルド後に自動配置されます。
実行ファイルだけを別のフォルダーに移すと起動できません。

## 必要なもの

- Visual Studio 2026 の「C++ によるデスクトップ開発」
- MSVC v145 の x64/x86 ビルドツール、Windows SDK、Windows 用 C++ CMake ツール（CMake 4.2 以上）
- 7-Zip（初回の Qt 自動展開に使用。既存の Qt を指定する場合は不要）
- 初回の依存ライブラリ取得時のみインターネット接続

スクリプトは Visual Studio のインストール先を `vswhere` で検出します。
CMake を PATH に登録したり、Developer Command Prompt を開いたりする必要はありません。
Qt VS Tools 拡張も不要です。

初回は Qt 5.15.2 の MSVC 2019 x64 版と SDL2 2.32.10 の VC 版を公式配布元から
`.deps/` 内に取得します。アーカイブは固定の SHA-256 で検証します。
既存ソースが Qt 5 API を使うため、Qt 6 には移行していません。

## コマンドで生成・ビルドする

リポジトリのルートで実行します。

```powershell
# ソリューションを生成する（通常はこれだけで Visual Studio からビルド可能）
.\Generate-VS2026.cmd

# 生成と Release ビルド
.\Generate-VS2026.cmd -Build

# 生成と Debug ビルド
.\Generate-VS2026.cmd -Build -Configuration Debug

# Visual Studio 更新後、古いコンパイラのパスが残っている場合
.\Generate-VS2026.cmd -Fresh -Build

# 既存の Qt 5 MSVC x64 版と SDL2 VC 版を使う
.\Generate-VS2026.cmd -QtRoot C:\Qt\5.15.2\msvc2019_64 -SDL2Root C:\Libraries\SDL2-2.32.10
```

Qt には Widgets、OpenGL、Multimedia と `windeployqt.exe` が必要です。
MinGW 用の Qt は使用できません。

生成されたソリューションは CMake が管理します。ソースやビルド設定は `CMakeLists.txt` を
編集してください。別の場所へリポジトリをコピーしたときは、`build/` を引き継がずに再生成します。
`.deps/` と `build/` は Git 管理対象外です。

## この構成の対象

- Windows x64、Qt GUI、OpenGL／ソフトウェア描画、SDL2 音声・入力
- Musashi 68000 コア、Play! の JIT コア
- Debug、Release、RelWithDebInfo、MinSizeRel の構成を生成
- 追加の依存が必要な SSF プレイヤーと MPEG デコードは無効
- 旧 DirectX SDK に依存する DirectSound／DirectInput は無効（音声・入力には SDL2 を使用）

ビルドには BIOS やディスクイメージは必要ありません。ゲームの動作確認は別途必要です。
既存コードのコンパイラ警告は残っています。

検証済み環境: Visual Studio Community 2026 18.12.0、MSVC 19.52、Windows SDK 10.0.26100.0。
`Release | x64` と `Debug | x64` のビルドが成功し、両方の `yabause.exe --version` が
`0.9.15` を出力して終了コード 0 で終了することを確認しました。ゲーム実行は未検証です。

## ゲームパッドの方向キー

ゲームパッドの方向キーが割り当て画面で反応してもゲーム中に効かない問題を修正しました。
修正前のバージョンでゲームパッドを登録した場合は、新しい実行ファイルで設定を開き、
入力方式 `SDL Joystick Interface` の上下左右を再割り当てしてください。
旧形式では軸の正負が保存されていないため、再割り当てが必要です。
入力処理の検証方法は [SDL 回帰テスト](yabause/tests/sdljoy/README.md) に記載しています。

## SDL 音声出力

音声バッファの未再生データ量を管理し、音声不足時に古い波形が繰り返される問題を修正しました。
再生開始時・音声不足からの復帰時には約 93 ms 分を蓄え、短い処理遅延を吸収します。
映像やゲーム速度の設定変更は不要です。
再初期化・NTSC/PAL 切り替え時のバッファ操作も保護しています。
検証方法は [SDL 音声回帰テスト](yabause/tests/sndsdl/README.md) に記載しています。
Visual Studio 2026 で音声テスト 2 件の成功、Debug／Release のビルドと
`--version` による起動を確認しました。実際のゲーム音声の試聴は未確認です。

## 参考

- [CMake の Visual Studio 2026 ジェネレーター](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html)
- [Qt 5.15.2 の公式バイナリ配布](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt5_5152/qt.qt5.5152.win64_msvc2019_64/)
- [SDL2 の公式配布](https://www.libsdl.org/release/)

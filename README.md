# EasyMix Studio

C++ / Dear ImGui / OpenGL / miniaudio 製の自作DAW。Windows と macOS に対応しています。

## 自動ビルド (GitHub Actions)
`main` ブランチへ push するか、Actions タブの **build → Run workflow** で、Windows と macOS の両方がビルドされます。
完成品は実行結果ページ下の **Artifacts** からダウンロードできます。

リリースとして配布する場合:
```sh
git tag v1.0.0
git push origin v1.0.0
```
タグ `v*` を push すると、`EasyMix-windows-x64.zip` と `EasyMix-macos.zip` が Releases に自動で添付されます。

## ローカルでビルド
必要なもの: CMake 3.21+、C++17 コンパイラ (Windows: Visual Studio 2022 / macOS: Xcode Command Line Tools)。
依存ライブラリ (GLFW / Dear ImGui docking / miniaudio / VST3 SDK) は CMake が自動で取得します (初回はネット接続が必要)。

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
- Windows: `build/Release/EasyMix.exe`
- macOS: `build/EasyMix.app`

## プラットフォーム別メモ
- VST3 の検索先
  - Windows: `C:\Program Files\Common Files\VST3`
  - macOS: `/Library/Audio/Plug-Ins/VST3` と `~/Library/Audio/Plug-Ins/VST3`
- macOS ではショートカットが Ctrl の代わりに Cmd になります
- macOS のレイアウトファイルは `~/Library/Application Support/EasyMix/imgui.ini` に保存されます
- macOS 版の `.app` は署名されていません。初回は右クリック →「開く」で起動してください
- `.emix` プロジェクトは両OSで開けますが、保存されている VST3 のパスはOSごとに異なるため、プラグインの再読み込みが必要です

## 構成
```
src/main.cpp          アプリ本体
src/Vst3Host.h        VST3 ホスト
src/Effects.h など    エフェクト / EQ / ピアノロール / スタイル
src/Platform*.h/.mm   macOS 用のファイルダイアログ・VST3エディタ窓 (Cocoa)
CMakeLists.txt        Windows / macOS 共通
.github/workflows/    CI
```

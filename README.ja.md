# CrossPoint Reader JP(非公式ビルド / Xteink X4 向け)

> **これは非公式のフォークです。** CrossPoint Reader の開発チーム、FreeInk、Xteink とは関係がありません。
> **無保証です。ファームウェアの書き換えは自己責任で行ってください。** 書き換えに失敗すると、本体が起動しなくなる可能性があります。必ず、事前にバックアップを取ってください。

[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) 2.0.0rc を元に、**日本語の本文を読むときの表示**を改善した、個人の改造版です。

## 何が変わるか

文字(アンチエイリアスの灰色を含む)のページを、次のように表示します。

| | 元の方式 | この改造版 |
|---|---|---|
| 表示の流れ | ① 黒一色の下地を表示(灰色の縁も黒になり、**太く粗く見える**)→ ② 灰色を重ねて細くする | **1 回の更新で、最終的なアンチエイリアス付きの画面**になる(粗い途中表示がない) |
| 1 ページの時間(X4 で実測) | 下地 + 灰色 1 回の更新: 約 1.0 秒 | 約 **0.65** 秒 |

- 上の「約 1.0 秒」は、元の方式と同じ構成(下地 + 灰色の更新 1 回)で、私が実装して測った値です(元の公式ファームそのものの測定値ではありません)。
- 純正の 4 階調波形(50 フレーム)は、1 回 1.08 秒かかります。この改造版は、フレームの長さを短くして 0.34 秒にしています(`0x22` → `0x88`)。

### 仕組み(概要)
1. 文字の描画を「画面の横帯ごと・1 回の走査」に変更し、2 つのグレー面を同時に作ります(一時的に使うメモリは、約 8〜19 KB です)。
2. その面を、表示ドライバの「窓書き込み」でコントローラーに送ります。
3. 白黒の下地を表示せずに、X4 に入っている純正の 4 階調波形で、一度に更新します。
4. 空きメモリが断片化していても動くよう、帯の数を自動で増やします。

## 動作確認の範囲(重要)

- **確認済み:** Xteink X4(1 台)、CrossPoint 2.0.0rc ベース、室温、日本語の EPUB(横書き・BIZ UD 系のフォント)、連続約 20 ページ。
- **未確認:**
  - 消費電力(増えるか減るかは測っていません)。
  - 寒い環境・暑い環境での濃さや残像。
  - 長時間・多数ページでの残像の溜まり。
  - X4 以外の機種(X4 Pro、X4 Classic、X3 など)。この改造は、X4 系(`gpio.isXteinkDevice()`)だけで有効になります。

## 既知の制約

- **クリップのハイライトが、この方式のページでは描かれません。**
- **薄い灰色・濃い灰色の網掛け(ディザ塗り)は、この方式のページには描かれません。**
- **画像を含むページは、元の方式で表示します。**
- 4 階調波形はフレームを速くしているため、電圧をかける時間が短くなっています。環境によっては、黒が薄い、残像が残る、といった症状が出る可能性があります。その場合は `kFactoryGrayFrameRate`(`freeink-sdk` の `Ssd1677Driver.cpp`)を `0x66` や `0x55` に下げるか、`ABSOLUTE_TEXT_AA`(`EpubReaderActivity.cpp`)を `false` にして元の方式へ戻してください。
- 公式の更新(OTA など)を適用すると、この改造は上書きされて消えます。
- バージョン表記が `2.0.0-jp.1` になるため、公式の更新確認では、別のバージョンとして扱われる可能性があります。

## インストール

> 以下は ESP32-C3 のフラッシュ書き込みツール **esptool** を使います(`pip install esptool`)。`<PORT>` は、本体を USB 接続したときのシリアルポート名です(macOS なら `/dev/cu.usbmodem…`、Windows なら `COM3` など)。

### 1. バックアップ(必須)
```
esptool --port <PORT> --baud 921600 read-flash 0x0 0x1000000 x4_backup.bin
```
16 MB のファイルが作られます。**設定(Wi-Fi の情報などを含む可能性があります)が入っているので、公開しないでください。**

### 2. 書き込み
[Releases](../../releases) から `firmware.bin` をダウンロードします。ファイルの SHA-256 も確認してください。
```
esptool --port <PORT> --baud 921600 write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x10000 firmware.bin
```
- 書き込むのはアプリ領域(`app0`)だけです。ブートローダー、パーティション、設定領域には触れません。
- この手順は、`app0` から起動している本体で確認しました。公式の OTA で `app1` から起動している状態では、書き込んでも反映されません。その場合は、起動スロットを `app0` に戻す必要があります(OTA 管理領域 `0xe000` から `0x2000` バイトを消去すると、`app0` から起動します。この操作は、私の環境では試していません)。

### 3. 元に戻す
```
esptool --port <PORT> --baud 921600 write-flash 0x0 x4_backup.bin
```
バックアップの全体を書き戻します(設定も元に戻ります)。

## ビルド

```
git clone --recurse-submodules https://github.com/homunet/crosspoint-reader-jp.git
cd crosspoint-reader-jp
git checkout jp/2.0.0rc
pio run -e x4_jp
```
- `freeink-sdk` は、このフォークに変更を加えたもの(`jp/window-gray` ブランチ)を指しています。
- **PlatformIO は 6.1.19 を使ってください。** 6.2.0 では、SCons の互換性の問題でビルドが失敗しました。
- ページごとの描画時間を見るには、`platformio.ini` の `x4_jp` で `LOG_LEVEL=2` にして、シリアルログを確認します。

## 日本語フォントの作り方

アンチエイリアスを濃くした日本語フォント(`.cpfont`)の作り方は、別のリポジトリにまとめています。
**<https://github.com/homunet/crosspoint-jp-fonts>**

## 変更の一覧

`2.0.0rc` から、次のコミットを重ねています(ファーム本体と `freeink-sdk`)。

- `feat(gfx): paint both gray planes in one glyph pass`
- `feat(gfx): render gray planes into a rectangular window target`
- `feat(reader): show text pages with the factory 4-level waveform`
- `build: add the x4_jp environment`
- (freeink-sdk)`feat(display): write a byte-aligned window of an overlay or absolute gray plane`
- (freeink-sdk)`feat(ssd1677): shorten the factory 4-level waveform with a faster frame rate`

## ライセンスと表記

- 元の CrossPoint Reader と freeink-sdk は **MIT ライセンス**です。このフォークの変更も、同じ MIT ライセンスで公開します。元の著作権表示は、`LICENSE` に残しています。
- 「CrossPoint Reader」「FreeInk」「Xteink」は、それぞれの権利者の名称です。このフォークは、それらの公式のものではありません。

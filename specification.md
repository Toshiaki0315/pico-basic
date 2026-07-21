# pico-basic 開発仕様書 (v2.0 - Waveshare RP2350-Touch-LCD-2.8対応版)

## 1. プロジェクト概要
本プロジェクトは、Raspberry Pi Pico 2（RP2350）を搭載した「Waveshare RP2350-Touch-LCD-2.8」ボード上で動作する、1980年代のHu-BASICと高い文法互換性を持つBASICインタプリタ **pico-basic** を開発する。
開発言語はC++20とし、Raspberry Pi Pico SDK (v2.0.0以上) を使用する。

## 2. 外部仕様書（ユーザーインターフェース・言語仕様）

### 2.1. 動作環境とI/O
* **ターゲットハードウェア:** Waveshare RP2350-Touch-LCD-2.8
* **主出力 (映像):** 2.8インチタッチLCD（解像度 240x320 または 320x240）。テキスト表示およびグラフィック描画を行う。
* **主入力:** USB CDC（シリアル通信）を用いたターミナル接続を基本のキーボード入力とする。加えて、LCDのタッチ入力をBASICから取得可能にする。
* **音声出力:** I2S DAC（PCM5101A）を PIO + DMA で駆動し、48kHz でサウンドを出力する。PSG 相当の矩形波・3重和音に対応し、発音キューを DMA 割り込みのシーケンサが進める**非同期再生**。起動時には PC-9801 風の 2 音（2000Hz→1000Hz）を鳴らす。
* **プログラム保存:** ボード上のMicroSDカードスロットを利用し、FatFS経由でテキスト形式のBASICプログラムを保存・読み込みする。

### 2.2. データ型
暗黙の型宣言をサポートする。
* **整数型 (`%`):** 16ビット符号付き整数（Hu-BASIC 準拠。範囲 -32768 〜 32767。内部表現は `int` 等でもよいが、オーバーフローは Hu-BASIC 相当とする）
* **単精度実数型 (`!` または省略時):** 32bit浮動小数点 (`float`)
* **倍精度実数型 (`#`):** `double` による近似（Hu-BASIC 互換の範囲）
* **文字列型 (`$`):** 最大長255文字の文字列
* **配列:** 1次元および2次元配列

### 2.3. サポートするコマンド・ステートメント
Hu-BASICのコア構文に加え、X1/MZライクなマルチメディア機能を実装する。

* **システムコマンド（MicroSD対応）:**
  * `NEW`, `LIST`, `RUN`
  * `FILES`: MicroSDカード内のファイル一覧表示。
  * `SAVE "ファイル名.BAS"`: プログラムをMicroSDに保存。
  * `LOAD "ファイル名.BAS"`: プログラムをMicroSDから読み込み。
* **制御ステートメント:**
  * `GOTO`, `GOSUB` / `RETURN`, `FOR` / `NEXT`, `IF` ~ `THEN` ~ `ELSE`, `END`, `STOP`
* **入出力・変数操作:**
  * `PRINT`, `INPUT`, `LET`, `DIM`, `READ`, `DATA`, `RESTORE`
* **グラフィック・テキスト制御（LCD対応）:**
  * `CLS`: 画面消去。
  * `LOCATE x, y`: テキストカーソルの位置移動。
  * `COLOR 文字色, 背景色`: カラーパレット（0〜15）の指定。
  * `PSET (x,y), 色`: 点の描画。
  * `LINE (x1,y1)-(x2,y2), 色, [B/BF]`: 線の描画（Bで矩形、BFで塗りつぶし矩形）。
  * `CIRCLE (x,y), 半径, 色`: 円の描画。
* **サウンド制御（I2S DAC 出力）:**
  * `BEEP`: 単音のビープ音（880Hz / 200ms）。
  * `MUSIC "文字列"[, "文字列", "文字列"]` / `PLAY ...`: MML（Music Macro Language）による音楽演奏。`PLAY` は `MUSIC` と同一のエイリアス。カンマ区切りで最大 3 声を同時発音する（例: `PLAY "O4CEG","O4EGB","O5C"`）。**非同期再生**（演奏を待たずに次の行へ進む。Ctrl-C / `NEW` で停止）。`V`（音量）は声ごとに独立で、既定値は 8。
  * `SOUND 周波数, 長さ`: 周波数 [Hz] と長さ [ms] を指定した発音。Hu-BASIC 本来の `SOUND reg, data`（PSG レジスタ書き込み）とは異なる暫定仕様。
* **ハードウェア制御（Pico拡張）:**
  * `TOUCH(n)`: タッチパネルの状態取得。n=0でX座標、1でY座標、2でタッチ有無(0/1)。
  * `WAIT ミリ秒`: 指定時間のウェイト。

---

## 3. 内部仕様書（アーキテクチャ・データ構造）

### 3.1. システムアーキテクチャ
以下の独立したモジュールで構成する。

1. **HAL (Hardware Abstraction Layer):** Pico SDKの機能をラップする層。
   * `hal_display`: SPI経由でのLCD初期化、文字フォントの描画、グラフィックプリミティブ（Bresenhamの線引きアルゴリズム等）の実装。
   * `hal_sdcard`: SDIO(4bit / PIO)経由でのMicroSDアクセスとFatFSの統合。ピン定義は `src/hw_config.c`。
   * `hal_sound`: PIO（`src/pio/i2s_out.pio`）+ DMA による I2S 出力。3声分の矩形波をダブルバッファ上でミックスし、DMA 完了割り込みで空いた側を埋める。発音は 128 段のキューに積み、割り込み内のシーケンサがサンプル単位で進めるため、演奏中も BASIC を実行できる。MML の解釈と声の時間軸合成は parser 側（`execute_music`）が担当する。
   * `hal_touch`: I2C(i2c1)経由でのタッチコントローラ CST328 の読み取り。10ms 間隔でポーリングし、パネル座標を表示の向き（320×240 横）に変換する。
2. **REPL / Line Editor:** シリアル入力を受け付ける。入力した内容はシリアルにエコーバックすると同時に、LCD画面の最下段等にリアルタイム表示（またはスクロール表示）する。
3. **Lexer (字句解析器):** 入力文字列をTokenに変換。
4. **Parser / Interpreter (構文解析・実行エンジン):** Token配列を順次読み込み実行。
5. **Memory Manager:** RP2350の520KBのSRAMを以下のように配分目安とする。
   * LCDフレームバッファ（必要に応じて）：約150KB (320x240 16bitカラーの場合。メモリ節約のため直接SPI転送も可)。
   * BASICテキストエリア：約128KB。
   * 変数テーブル・ヒープ：約128KB。
   * 残りはPico SDKのスタック、FatFSバッファ等。

---

## 4. 開発フェーズ定義（進捗と残課題）

ハードウェア依存が大きいため、段階的に検証する。**詳細な対応表は [docs/PHASE_RUNTIME.md](docs/PHASE_RUNTIME.md)**、制御・ファイル系の差分は **[docs/RUNTIME_GAPS.md](docs/RUNTIME_GAPS.md)** を参照。

### Phase 1: REPL とコアエンジンの確立 — **達成（継続保守）**

* Pico SDK の CMake ターゲット（`pico_basic`）とホストテスト（`BUILD_TESTS=ON`）。
* USB CDC REPL、四則演算、`PRINT`、変数、`lexer`/`parser` のコア。

### Phase 2: LCD 表示の統合 — **達成（部分仕様は未）**

* Waveshare 向け SPI 初期化・フレームバッファ、`PRINT` の LCD エコー。
* **テキスト:** 8×8 ドットフォント、グリッド上のカーソル、行末折返し、下端での **上スクロール**（`memmove`）、`CLS` / `LOCATE`。
* **未対応:** `CONSOLE` / `WIDTH` によるテキストウィンドウ分割（parser 側は未実装通知）。

### Phase 3: プログラムの保存と制御構文 — **実装完了（実機確認待ち）**

* **制御構文:** `GOTO` / `GOSUB` / `RETURN` / `FOR` / `NEXT` / `IF` … `THEN` / `ELSE` 等は実行パスあり（Hu-BASIC 全命令との一致は [RUNTIME_GAPS.md](docs/RUNTIME_GAPS.md)）。
* **ファイル:** ホストでは `SAVE` / `LOAD` / `FILES` が stdio で動作。**Pico では FatFS + MicroSD（SDIO 4bit / PIO 実装）** で動作（`hal_sdcard.cpp` / ピン定義は `src/hw_config.c`）。

### Phase 4: グラフィックコマンド — **達成（性能計測は任意）**

* `PSET`, `LINE`, `CIRCLE`, `COLOR`, `PAINT`, `GET@`, `PUT@` を parser から `hal_display` に接続済み。
* 実機でのリフレッシュレート・SPI 帯域の評価は運用ドキュメントに逐次追記する。

### Phase 5: サウンドとタッチ — **実装完了（タッチは実機確認待ち）**

* parser から `BEEP` / `PLAY` / `MUSIC` / `SOUND`、`TOUCH(n)` を呼び出し可能。
* **Pico 上:** `hal_sound.cpp` は I2S DAC 出力を実装済み（矩形波・3重和音・非同期再生）。`hal_touch.cpp` は CST328 の I2C 読み取りを実装済み。

---

**解像度メモ:** `hal_display` のラスタ実装は **320×240** ピクセル（16bit フレームバッファ）を前提とする。§2.1 の「240×320 または 320×240」はボードの向きに依存し、MADCTL 設定と一致させること。


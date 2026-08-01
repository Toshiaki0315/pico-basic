# pico-basic

**Raspberry Pi Pico 2 (RP2350) 上で動作するネイティブ BASIC インタプリタ**です。

1980年代のマイコンの操作感 — 電源を入れたら `Ready`、行番号を打って `RUN` — をそのままに、カラー液晶・タッチパネル・MicroSD・I2S 音源を BASIC から直接扱えます。言語・グラフィック・サウンド・ストレージまで実装済みで、実機での動作検証も一巡しています。

SHARP X1 turbo (CZ-8FB02) の **Hu-BASIC を土台に、独自に拡張した仕様**です。シャープ S-BASIC の `AUTO`、NEC N88-BASIC のファイル I/O・エラー処理・編集コマンドなどを取り込み、タッチパネルや画面キャプチャといった本機固有の命令も加えています。**Hu-BASIC の互換実装ではない**ため、当時のプログラムがそのまま動くとは限りません（命令ごとの出自は [MANUAL.md](./MANUAL.md) の「この BASIC の成り立ち」）。

```basic
10 REM お絵かき（指でなぞる）
20 IF TOUCH(2) THEN PSET (TOUCH(0), TOUCH(1)), 15
30 GOTO 20
```

## ✨ 特徴

| 分野 | 内容 |
| :--- | :--- |
| **言語** | `IF`/`ELSEIF`、`FOR`/`NEXT`、`WHILE`/`WEND`、`REPEAT`/`UNTIL`、`GOSUB`（文単位復帰）、行ラベル `*NAME`、`DEF FN`、`ON ERROR GOTO`/`RESUME`、2次元・文字列配列、`MOD`/`\`/`AND`/`OR`/`XOR`/`NOT`、`&H`/`&B` リテラル |
| **グラフィック** | 320×240・16色。`PSET`/`LINE`(B/BF)/`CIRCLE`/`POLY`/`PAINT`、スプライト（`GET@`/`PUT@` XOR・拡大縮小・論理合成）、ユーザー座標系 `WINDOW`、画素読み取り `POINT`、ちらつき防止の `SYNC` |
| **サウンド** | PSG（AY-3-8910 相当）: 3声矩形波＋ノイズ＋エンベロープ。MML の `PLAY`（3声・**非同期再生**）、レジスタ直接制御の `SOUND` |
| **入力** | タッチパネル `TOUCH()`、ノンウェイトキー入力 `GET`、電池残量 `BATTERY()` |
| **センサー** | 6軸 IMU `ACCEL()` / `GYRO()`（傾け操作）、内蔵温度 `CPUTEMP`、時計 `TIME$` / `DATE$`（バックアップ電池で保持） |
| **外部接続** | GPIO 出力 `GPIO`、デジタル入力 `PIN()`、アナログ入力 `ADIN()`（GPIO28/29） |
| **日本語** | JIS X 0201 **半角カタカナ**（`&HA1`〜`&HDF`）を 8×8 字形で表示。X1 turbo と同じ方式 |
| **ストレージ** | MicroSD に `SAVE`/`LOAD`（プログラム）、`OPEN`/`PRINT #`/`INPUT #`/`EOF`（データファイル。ハイスコア保存など）。カードの抜き差しにも追従 |
| **開発体験** | **Ctrl-P で画面を BMP 保存**、`AUTO`（行番号自動生成）、`RENUM`（飛び先も書き換え）、範囲 `LIST`、`DELETE`、`CONT`（Ctrl-C の続きから再開）、`TRON`/`TROFF`、`PRINT USING`、エラーコード表示 |

実装済み命令の全一覧は [docs/COMMAND_STATUS.md](./docs/COMMAND_STATUS.md) を参照してください。

## 🎯 ターゲットハードウェア

* **Raspberry Pi Pico 2**（RP2350）
* **[Waveshare RP2350-Touch-LCD-2.8](https://www.waveshare.com/wiki/RP2350-Touch-LCD-2.8)**（2.8インチ SPI LCD・静電容量タッチ・MicroSD スロット・I2S DAC 内蔵ボード）

## 🚀 クイックスタート

1. `pico_basic.uf2` をボードに書き込みます（ビルド手順は [SETUP.md](./SETUP.md)）。
2. USB でシリアル端末（VS Code の Pico Serial Monitor、`screen` など）を開くと `Ready` が出ます。
3. そのまま打てば動きます:

```basic
PRINT "HELLO, PICO!"
PLAY "O4CEG"
CIRCLE (160,120), 50, 14
```

4. サンプルを SD カードに入れて遊ぶ場合:

```basic
LOAD "QIX.BAS"
RUN
```

## 🎮 サンプルプログラム（[samples/](./samples/)）

| ファイル | 内容 |
| :--- | :--- |
| `qix.bas` | **陣取りゲーム**（クイックス風）。フラッドフィルで領地判定、ハイスコアを SD に保存 |
| `ball.bas` | XOR スプライトで跳ねるボール |
| `draw.bas` | タッチでお絵かき |
| `song.bas` | 非同期演奏＋ビジュアライザ |
| `sfx.bas` | PSG 効果音（レーザー・爆発） |
| `art.bas` / `guess.bas` | 幾何アート / 数当てゲーム |
| `kana.bas` | 半角カタカナ一覧（日本語表示のデモ） |
| `tilt.bas` | 傾けてボールを転がす（6軸 IMU） |

## 📚 ドキュメント

| ドキュメント | 内容 |
| :--- | :--- |
| **[MANUAL.md](./MANUAL.md)** | ユーザーマニュアル（全命令の使い方・エラー処理・エラーコード表） |
| [SETUP.md](./SETUP.md) | 開発環境構築（macOS / VS Code）と実機への書き込み |
| [specification.md](./specification.md) | 開発仕様書 |
| [docs/COMMAND_STATUS.md](./docs/COMMAND_STATUS.md) | 命令ごとの実装状況 |
| [docs/DEVICE_CHECKLIST.md](./docs/DEVICE_CHECKLIST.md) | 実機の手動テストチェックリスト |
| [docs/TESTING.md](./docs/TESTING.md) | ホストテストと CI の運用 |
| [docs/PHASE_RUNTIME.md](./docs/PHASE_RUNTIME.md) / [docs/RUNTIME_GAPS.md](./docs/RUNTIME_GAPS.md) | 開発フェーズ対応・Hu-BASIC との差分メモ |

## 📁 ディレクトリ構成

```text
├── .agent/           # AI エージェント向け仕様・プロンプト
├── docs/             # 補助ドキュメント（命令状況・テスト・実機チェックリスト等）
├── samples/          # サンプル BASIC プログラム（ゲーム・デモ）
├── src/              # BASIC エンジン（lexer / parser / repl）と HAL（hal_*.cpp）
├── tests/            # ホスト単体テスト（Google Test、BUILD_TESTS=ON）
├── specification.md  # 開発仕様書
├── MANUAL.md         # ユーザーマニュアル
├── SETUP.md          # 開発環境構築ガイド
└── README.md         # 本ドキュメント
```

ハードウェア依存コードは **`src/hal_*.cpp`** に集約しています。BASIC エンジン本体（lexer / parser / メモリ管理）はハードウェア非依存で、ホスト PC 上でそのままテストできます。

## 🔨 ビルドとテスト

**実機用ファームウェア**のビルドと書き込みは [SETUP.md](./SETUP.md) を参照してください。

**ホストテスト**（実機不要）: BASIC エンジンは PC 上で 360 件超の単体テストで検証しています。

```bash
cmake -S . -B build_host -DBUILD_TESTS=ON && cmake --build build_host && ./build_host/basic_tests
```

GitHub では `.github/workflows/ci.yml` が同じテストを自動実行します。実機のみで確認できる項目（LCD・タッチ・音・SD）は [docs/DEVICE_CHECKLIST.md](./docs/DEVICE_CHECKLIST.md) で管理しています。

## 📖 最初の一歩

```basic
10 CLS
20 FOR I = 1 TO 5
30 PRINT USING "##: HELLO"; I
40 NEXT I
50 PLAY "O5CEG"
RUN
```

行番号付きで入力するとプログラムとして記憶され、`RUN` で実行、`LIST` で一覧、`SAVE "NAME.BAS"` で SD に保存できます。`AUTO` と打てば行番号は自動で振られます。実行が止まらないときは **Ctrl-C**（`CONT` で続きから再開できます）。

続きは **[MANUAL.md](./MANUAL.md)** へ。

---

*Enjoy coding on Raspberry Pi Pico!*

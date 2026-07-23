# 言語・ランタイム — 仕様書フェーズと実装の対応

`specification.md` §4 の開発フェーズと、ソース・テスト・手動確認の対応表です。

## Phase 1 — REPL とコアエンジン

| 項目 | 状態 | 参照 |
| :--- | :--- | :--- |
| CMake / Pico ターゲット | 実装 | `CMakeLists.txt` |
| USB シリアル REPL | 実装 | `src/repl.cpp`, `src/main.cpp` |
| 四則演算、`PRINT`、変数、lexer/parser | 実装 | `src/lexer.cpp`, `src/parser.cpp` |
| ホスト単体テスト | 実装 | `tests/*.cpp`, `BUILD_TESTS=ON` |

## Phase 2 — LCD テキスト（`PRINT` / `CLS` / `LOCATE` / スクロール）

| 項目 | 状態 | 参照 |
| :--- | :--- | :--- |
| SPI LCD 初期化（ST7789 系） | 実装 | `src/hal_display.cpp`（`__has_include("pico/stdlib.h")` 側） |
| テキストグリッド | 実装 | 8×8 フォント、`TEXT_COLS` × `TEXT_ROWS`（320×240 想定） |
| 縦スクロール | 実装 | `scroll_up()` + 最下行クリア（`memmove`） |
| `CLS` / `LOCATE` | 実装 | `hal_display_cls`, `hal_display_locate` |
| テキスト領域（`CONSOLE`）や部分スクロール | 未実装 | parser は `execute_not_implemented`（[COMMAND_STATUS](./COMMAND_STATUS.md)） |

**実機確認:** [DEVICE_CHECKLIST.md](./DEVICE_CHECKLIST.md) の B 項。フレームバッファ全転送の頻度はグラフィック命令側の `hal_display_sync` に依存（テキストのみの更新タイミングは運用で確認）。

## Phase 3 — 保存（FatFS）と制御構文

| 項目 | 状態 | 参照 |
| :--- | :--- | :--- |
| `SAVE` / `LOAD` / `FILES` | ホストは stdio / `opendir` | `parser.cpp` の `execute_*` |
| MicroSD + FatFS | 実装（実機確認待ち） | `hal_sdcard.cpp` + `src/hw_config.c`。SDIO 4bit（CLK=19 / CMD=20 / D0〜D3=21〜24、pio1 + DMA_IRQ_1）、ドライバは carlk3 の FatFS ライブラリ |
| `GOTO` / `GOSUB` / `RETURN` / `FOR` / `NEXT` / `IF` | 実装 | `parser.cpp` |
| Hu-BASIC 追加構文（`REPEAT`/`UNTIL`、`GET`、`WINDOW` 等） | 未実装パス | `execute_not_implemented` |

**ギャップ一覧:** [RUNTIME_GAPS.md](./RUNTIME_GAPS.md)

## Phase 4 — グラフィック命令

| 項目 | 状態 | 参照 |
| :--- | :--- | :--- |
| `PSET` / `LINE` / `CIRCLE` / `COLOR` / `PAINT` / `GET@` / `PUT@` | parser → HAL | `parser.cpp`, `hal_display.cpp` |
| フレームバッファ → SPI 転送 | 実装 | `hal_display_sync()` |
| 実機 FPS・帯域の計測 | 未ドキュメント | 必要時に `picotool` / ロジックアナライザ等で別途記録 |

## Phase 5 — サウンド・タッチ

| 項目 | 状態 | 参照 |
| :--- | :--- | :--- |
| `BEEP` / `MUSIC` / `PLAY` | 実装 | `parser.cpp` → `hal_sound.cpp` |
| `SOUND`（PSG レジスタ） | 実装（実機確認待ち） | AY-3-8910 相当。トーン3ch・ノイズ・音量。エンベロープ未 |
| Pico 上の実音出力 | 実装（実機確認済み） | `hal_sound.cpp` + `src/pio/i2s_out.pio` — I2S DAC（PCM5101A）を PIO+DMA で駆動。BCK=GP2 / LRCK=GP3 / DIN=GP4、48kHz |
| 矩形波（PSG）化・3重和音 | 実装 | `hal_sound.cpp` で 3声の矩形波をミックス。`MUSIC "A","B","C"` で同時発音 |
| 非同期再生 | 実装（実機確認待ち） | 発音キュー + DMA 割り込みのシーケンサ。`PLAY` は待たずに戻り、Ctrl-C / `NEW` で停止 |
| `TOUCH(n)` | 実装（実機確認待ち） | `hal_touch.cpp` — CST328 を i2c1（SDA=6 / SCL=7、RST=17 / INT=18）で読み取り |

---

更新ルール: フェーズ完了時に本ファイルと `specification.md` §4 の表を同期する。

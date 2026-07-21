# 命令・機能の実装状況

`src/parser.cpp` / `src/lexer.cpp` とホスト単体テスト（`BUILD_TESTS=ON`）の整理に基づく一覧です。実機（Pico）では HAL のスタブ／未リンク箇所により挙動が異なる場合があります。

開発フェーズ全体との対応は **[PHASE_RUNTIME.md](./PHASE_RUNTIME.md)**、Hu-BASIC との差分は **[RUNTIME_GAPS.md](./RUNTIME_GAPS.md)** を参照してください。

## 凡例

| 記号 | 意味 |
| :--- | :--- |
| **実装** | 実行パスがあり、ホストテストまたは実機で主用途が動く |
| **部分** | 認識・分岐はあるがスタブ・簡易実装、または I/O がホスト依存 |
| **未実装** | 字句は認識するが実行時に「未実装」通知でスキップされる（`execute_not_implemented`） |

## システム・プログラム管理

| 命令 | 状態 | メモ |
| :--- | :--- | :--- |
| `NEW` / `CLEAR` | 実装 | プログラム・変数の消去 |
| `LIST` | 実装 | プログラム一覧 |
| `RUN` | 実装 | 行番号昇順で実行 |
| `SAVE` / `LOAD` | 部分 | `fopen` 等。Pico では FatFS / SD 統合が別途必要 |
| `FILES` | 部分 | `opendir` カレント。SD ルート連携は未 |
| `KILL` / `NAME` | 部分 | 同上（ファイル API 依存） |

## 制御・代入

| 命令 | 状態 | メモ |
| :--- | :--- | :--- |
| `GOTO` / `GOSUB` / `RETURN` | 実装 | |
| `IF` … `THEN` / `ELSE` | 実装 | |
| `FOR` / `NEXT` | 実装 | |
| `END` / `STOP` | 実装 | |
| `LET` / 代入 | 実装 | `LET` 省略可 |
| `DIM` | 実装 | 1D / 2D |
| `PRINT` / `INPUT` | 実装 | |
| `READ` / `DATA` / `RESTORE` | 実装 | |
| `ON` … `GOTO`/`GOSUB` | 実装 | 分岐テーブル |

## グラフィック・表示

| 命令 | 状態 | メモ |
| :--- | :--- | :--- |
| `CLS` | 実装 | |
| `LOCATE` | 実装 | |
| `COLOR` | 実装 | パレット索引 |
| `PSET` / `LINE` / `CIRCLE` | 実装 | HAL 描画 |
| `PAINT` | 実装 | 塗りつぶし |
| `GET@` / `PUT@` | 実装 | 配列とピクセル転送 |
| `WINDOW` | 未実装 | |
| `POLY` | 未実装 | |
| `BRIGHTNESS`（拡張） | 実装 | ディスプレイ依存 |

## サウンド・その他ハード

| 命令 | 状態 | メモ |
| :--- | :--- | :--- |
| `BEEP` | 実装 | 実機は I2S DAC で発音（880Hz / 200ms）、ホストはログ |
| `MUSIC` / `PLAY` | 実装 | 同一 `execute_music`（MML サブセット）。単音・ブロッキング再生 |
| `SOUND` | 部分 | 実装は `SOUND 周波数, 長さ`。Hu-BASIC の `SOUND reg, data`（PSG レジスタ）とは非互換 |
| `WAIT` | 実装 | |
| `GPIO` | 部分 | Pico 定義時は実 GPIO、ホストはモック |
| `TOUCH(...)` 関数 | 部分 | 実機 I2C 未接続時はスタブ値 |

## 字句のみ・実行時未実装

次は **`execute_not_implemented`** 経由（実行すると通知メッセージ）:

`INIT`, `NEWON`, `WIDTH`, `CONSOLE`, `REPEAT`, `UNTIL`, `GET`

---

更新するときは `parser.cpp` の `execute_statement` と字句解析（`lexer.cpp`）を正としてください。

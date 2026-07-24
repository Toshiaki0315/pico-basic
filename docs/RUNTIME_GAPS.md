# ランタイム・Hu-BASIC 準拠のギャップ（Phase 3 補足）

`parser.cpp` の `execute_statement` と `.agent/hubasic-spec.md` を照合した **現状の差分**です。優先度はプロジェクト方針で更新してください。

## 1. 実行時「未実装」通知（字句は受理）

未実装通知（`execute_not_implemented`）で終わる命令は現在ありません。

| 命令 | メモ |
| :--- | :--- |
| `INIT`, `NEWON` | 互換のための空実装（引数を受理して何もしない）。元の X1 Hu-BASIC のメモリ領域予約に対応する概念がこの実装には無い |

## 2. ファイル I/O（MicroSD / FatFS）

| 命令 | ホスト | Pico 実機（現状） |
| :--- | :--- | :--- |
| `SAVE` / `LOAD` / `FILES` / `KILL` / `NAME` | `fopen` / `opendir` 等で動作 | FatFS + MicroSD（SDIO 4bit / PIO 実装）で動作（`hal_sdcard.cpp`）。カード未挿入・未フォーマット時は操作が失敗する |

## 3. 制御構文で既に実装済み（参照用）

`GOTO`, `GOSUB`, `RETURN`, `FOR`, `NEXT`, `IF`/`THEN`/`ELSEIF`/`ELSE`, `REPEAT`/`UNTIL`, `END`, `STOP`, `ON` … `GOTO`/`GOSUB`, `DIM`, `READ`/`DATA`/`RESTORE`, `INPUT`, `GET`, `LET` 代入など。`GOTO`/`GOSUB`/`IF … THEN` は行番号のほか `*ラベル` を受け付ける。

**行番号単位で戻る制御構文の制限:** `GOSUB`/`RETURN`・`FOR`/`NEXT`・`REPEAT`/`UNTIL` はいずれも
「戻り先＝行番号」で管理し、文（`:` 区切り）単位では戻れない。したがって次の書き方は避ける:

- `GOSUB 100 : PRINT X` … `RETURN` は**次の行**へ戻るので、同じ行の `PRINT X` は実行されない。`GOSUB` は行末（その行の最後の文）に置く。
- `FOR J=1 TO 3 : … : NEXT J` … 単一行に収めた `FOR`/`NEXT` はループせず 1 回で抜ける。`FOR` と `NEXT` は別々の行に置く。
- `REPEAT` は行頭に置く（`REPEAT` の直後に `:` で文を続けると戻り先がずれる）。

いずれも行番号ベースの実行ループ（`run_program`）に由来する。文単位の再開に対応するには呼び出し
スタックに「行内の位置」も積む必要があり、影響が大きいため現状は上記を運用ルールとする。

## 4. 今後の仕様検討メモ

- **整数 `%` のオーバーフロー**: 仕様は 16bit。内部 `int` との整合テストを増やす価値あり。  
- **エラーコード表示**: 実装済み。`Error <code> in line N: <msg>`（`parser.cpp` の `basic_error_code`）。コードはメッセージのキーワードから推定するため、新しいエラー文言を足したらこの対応表も更新すること。

---

本ドキュメントは [PHASE_RUNTIME.md](./PHASE_RUNTIME.md) の Phase 3 と [COMMAND_STATUS.md](./COMMAND_STATUS.md) を補完します。

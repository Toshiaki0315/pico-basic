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

**`REPEAT`/`UNTIL` の制限:** `FOR`/`NEXT` と同じく行番号で戻るため、`REPEAT` は行頭に置くこと（同一行に `:` で他の文を続けると戻り先がずれる）。

## 4. 今後の仕様検討メモ

- **整数 `%` のオーバーフロー**: 仕様は 16bit。内部 `int` との整合テストを増やす価値あり。  
- **エラーコード表示**: 実装済み。`Error <code> in line N: <msg>`（`parser.cpp` の `basic_error_code`）。コードはメッセージのキーワードから推定するため、新しいエラー文言を足したらこの対応表も更新すること。

---

本ドキュメントは [PHASE_RUNTIME.md](./PHASE_RUNTIME.md) の Phase 3 と [COMMAND_STATUS.md](./COMMAND_STATUS.md) を補完します。

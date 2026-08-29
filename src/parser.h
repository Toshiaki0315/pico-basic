#pragma once
#include "lexer.h"

bool parse_and_execute(const TokenList& tokens);

// BASIC の出力（LCD とシリアル端末の両方へ）
void basic_print(const char* s);

// Ctrl-C で実行中のプログラムを止める（CONT で再開できる状態にする）。
// 実行ループの定期チェックと、入力待ちの最中に読んだ Ctrl-C の両方から呼ぶ
void basic_break_program();

// 電源を切る（POWEROFF 文と電源ボタンの長押しの共通処理）。
// 電源が落ちれば戻らない。戻ったときは切れなかったということで、
// 開いていたファイルは閉じたうえで実行中のプログラムを停止させてある。
void basic_power_off();

// 乱数の種を時刻から作る（RUN のたびに撒き直すのに使う）
unsigned int basic_random_seed_source();
void store_line(int line_number, const TokenList& tokens);
void list_program(int from_line = 0, int to_line = 65535);
void clear_program();
void run_program(int max_steps = -1);

// AUTO コマンド（行番号自動生成）。直前の parse_and_execute が AUTO を実行したら
// true を返し、開始番号と刻みを *start / *step に入れて保留フラグをクリアする。
// 対話入力を持つ repl_start がこれを見て自動行番号モードに入る。
bool auto_mode_requested(int* start, int* step);

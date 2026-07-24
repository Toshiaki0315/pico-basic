#pragma once
#include "lexer.h"

bool parse_and_execute(const TokenList& tokens);

// BASIC の出力（LCD とシリアル端末の両方へ）
void basic_print(const char* s);
void store_line(int line_number, const TokenList& tokens);
void list_program(int from_line = 0, int to_line = 65535);
void clear_program();
void run_program(int max_steps = -1);

// AUTO コマンド（行番号自動生成）。直前の parse_and_execute が AUTO を実行したら
// true を返し、開始番号と刻みを *start / *step に入れて保留フラグをクリアする。
// 対話入力を持つ repl_start がこれを見て自動行番号モードに入る。
bool auto_mode_requested(int* start, int* step);

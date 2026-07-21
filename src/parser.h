#pragma once
#include "lexer.h"

bool parse_and_execute(const TokenList& tokens);

// BASIC の出力（LCD とシリアル端末の両方へ）
void basic_print(const char* s);
void store_line(int line_number, const TokenList& tokens);
void list_program();
void clear_program();
void run_program(int max_steps = -1);

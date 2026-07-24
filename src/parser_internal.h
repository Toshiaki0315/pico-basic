#pragma once
#include "lexer.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <cmath>

// ---------------------------------------------------------
// Data Representation
// ---------------------------------------------------------
struct Value {
    enum class Type { NUM, INT, STR };
    Type type;
    float num_val;
    int int_val;
    char str_val[128];

    Value() : type(Type::NUM), num_val(0.0f), int_val(0) { str_val[0] = '\0'; }
    Value(float n) : type(Type::NUM), num_val(n), int_val(0) { str_val[0] = '\0'; }
    Value(int i) : type(Type::INT), num_val((float)i), int_val(i) { str_val[0] = '\0'; }
    Value(const char* s) : type(Type::STR), num_val(0.0f), int_val(0) {
        strncpy(str_val, s, sizeof(str_val) - 1);
        str_val[sizeof(str_val) - 1] = '\0';
    }

    float get_num() const {
        return num_val;
    }

    const char* c_str() const {
        if (type == Type::STR) return str_val;
        static char buf[32];
        if (type == Type::INT) snprintf(buf, sizeof(buf), "%d", int_val);
        else snprintf(buf, sizeof(buf), "%g", num_val);
        return buf;
    }
};

struct ArrayRef {
    char name[9];        // up to 8-char name + null terminator
    uint16_t start_addr;
    uint16_t dim1;       // 1D: total elements; 2D: rows
    uint16_t dim2;       // 1D: 0;              2D: cols
    uint8_t  ndim;       // 1 or 2
    bool active;
    uint16_t total_size() const {
        return ndim == 2 ? (uint16_t)(dim1 * dim2) : dim1;
    }
};

struct ForLoopContext {
    char var_name[64];
    float target;
    float step;
    int loop_start_line;
    int loop_start_pos;
};

// ---------------------------------------------------------
// Constants
// ---------------------------------------------------------
#define MAX_VARIABLES 128
#define MAX_ARRAY_HEAP 1024
#define MAX_FOR_STACK 16
#define MAX_REPEAT_STACK 16
#define MAX_CALL_STACK 64
#define MAX_DATA_BUFFER 256
#define MAX_PROGRAM_LINES 256
#define MAX_LABELS 64

#define MEMORY_TEXT_BASE 0x0000
#define MEMORY_VAR_BASE 0x8000
#define VAR_TABLE_SIZE (MAX_VARIABLES * 16)
#define ARRAY_TABLE_BASE (MEMORY_VAR_BASE + VAR_TABLE_SIZE)
#define ARRAY_TABLE_SIZE (MAX_VARIABLES * 16)
#define DATA_HEAP_BASE (ARRAY_TABLE_BASE + ARRAY_TABLE_SIZE)
#define STRING_HEAP_BASE 0xC000 // Simple string heap

// ---------------------------------------------------------
// Shared State
// ---------------------------------------------------------
extern uint8_t logical_memory[65536];

extern uint16_t string_heap_ptr;
extern uint16_t array_heap_inner_ptr;
extern float last_rnd_val;

extern int current_line;
extern bool branch_taken;
// 分岐後に行内のどの位置から実行を再開するか。-1 は行頭（pos 0）から。
// RETURN / NEXT / UNTIL が「同じ行の続き」に戻るために使う（文単位の復帰）。
extern int branch_resume_pos;

extern uint16_t current_color_565;
extern const uint16_t PALETTE[16];

extern ForLoopContext for_stack[MAX_FOR_STACK];
extern int for_stack_ptr;

// REPEAT があった行番号を覚えておく。戻るときは FOR/NEXT と同じく
// 「その次の行」から実行を再開する（run_program は行頭から実行するため）
extern int repeat_stack_line[MAX_REPEAT_STACK];
extern int repeat_stack_pos[MAX_REPEAT_STACK];  // REPEAT 直後の行内位置（文単位の復帰用）
extern int repeat_stack_ptr;

extern int call_stack[MAX_CALL_STACK];
extern int call_stack_pos[MAX_CALL_STACK];       // GOSUB 呼び出し直後の行内位置（復帰先）
extern int call_stack_ptr;

// 行ラベル表。RUN のたびに run_program の先読みで作り直す。
// name は `*NAME` 形式（トークンのテキストと同じ）を大文字で保持する。
struct LabelEntry {
    char name[MAX_TOKEN_LEN];
    int  line;
};
extern LabelEntry label_table[MAX_LABELS];
extern int label_table_size;

// ラベル名（`*NAME`）から行番号を返す。見つからなければ -1。
int resolve_label(const char* name);

extern Value data_buffer[MAX_DATA_BUFFER];
extern int data_buffer_size;
extern int data_ptr;

// ---------------------------------------------------------
// Shared Functions
// ---------------------------------------------------------
const char* token_type_to_string(TokenType type);
void require_token(const TokenList& tokens, int& pos, TokenType expected, const char* err_msg);

// Memory / Program Managers
bool get_variable(const char* name, Value& out_val);
void set_variable(const char* name, const Value& val);
ArrayRef* get_array(const char* name);
void write_heap_value(uint16_t addr, const Value& val);
Value read_heap_value(uint16_t addr);

int flatten_array_index(const ArrayRef* arr, int i, int j = -1);
uint16_t find_program_line(int line_number);
uint16_t get_next_program_line(int line_number);

// Parsers & Executors
Value parse_expression(const TokenList& tokens, int& pos);
Value parse_relation(const TokenList& tokens, int& pos);
void execute_statement(const TokenList& tokens, int& pos);

// Statement Executors
// BASIC の出力は LCD とシリアル端末の両方へ出す。
// 片方だけに出す実装を書かないよう、必ずこの関数を経由すること
void basic_print(const char* s);

void execute_print(const TokenList& tokens, int& pos);
void execute_clear();
void execute_read(const TokenList& tokens, int& pos);
void execute_goto(const TokenList& tokens, int& pos);
void execute_gosub(const TokenList& tokens, int& pos);
void execute_return(const TokenList& tokens, int& pos);
void execute_if(const TokenList& tokens, int& pos);
void execute_for(const TokenList& tokens, int& pos);
void execute_next(const TokenList& tokens, int& pos);
void execute_dim(const TokenList& tokens, int& pos);
void execute_assignment(const TokenList& tokens, int& pos, bool explicit_let);
void execute_input(const TokenList& tokens, int& pos);
void execute_end(const TokenList& tokens, int& pos);
void execute_stop(const TokenList& tokens, int& pos);
void execute_repeat(const TokenList& tokens, int& pos);
void execute_until(const TokenList& tokens, int& pos);
void execute_get(const TokenList& tokens, int& pos);
void execute_on(const TokenList& tokens, int& pos);
void execute_mid_statement(const TokenList& tokens, int& pos);
void execute_not_implemented(const TokenList& tokens, int& pos);

void execute_color(const TokenList& tokens, int& pos);
void execute_pset(const TokenList& tokens, int& pos);
void execute_line(const TokenList& tokens, int& pos);
void execute_circle(const TokenList& tokens, int& pos);
void execute_locate(const TokenList& tokens, int& pos);
void execute_wait(const TokenList& tokens, int& pos);
void execute_gpio(const TokenList& tokens, int& pos);
void execute_brightness(const TokenList& tokens, int& pos);
void execute_paint(const TokenList& tokens, int& pos);
void execute_window(const TokenList& tokens, int& pos);
void execute_poly(const TokenList& tokens, int& pos);
void execute_console(const TokenList& tokens, int& pos);
void execute_width(const TokenList& tokens, int& pos);

// WINDOW で設定したユーザー座標系を既定（画面座標）に戻す
void reset_graphics_window();
void execute_get_at(const TokenList& tokens, int& pos);
void execute_put_at(const TokenList& tokens, int& pos);
void execute_save(const TokenList& tokens, int& pos);
void execute_load(const TokenList& tokens, int& pos);
void execute_kill(const TokenList& tokens, int& pos);
void execute_name(const TokenList& tokens, int& pos);
void execute_files(const TokenList& tokens, int& pos);
void execute_beep(const TokenList& tokens, int& pos);
void execute_sound(const TokenList& tokens, int& pos);
void execute_music(const TokenList& tokens, int& pos);

TokenList get_detokenized_line(uint16_t line_ptr);
void clear_program();
void list_program();
void store_line(int line_number, const TokenList& tokens);


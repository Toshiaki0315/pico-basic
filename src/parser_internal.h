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
// 収まらなければ切り詰めて、必ず終端する。
//
// strncpy は「入りきったときだけ終端する」ので、正しく使うには毎回
// 終端の 1 行が要る。その書き方は GCC が切り詰めの可能性として警告してくるため、
// 意図（切り詰めてよい・必ず終端する）を名前にして 1 か所にまとめる。
// src は終端済みの C 文字列であること（strnlen で上限を切ると、src が
// dst より小さい配列だったときに「上限まで読むかもしれない」と警告される）。
inline void copy_string(char* dst, size_t dst_size, const char* src) {
    if (dst_size == 0) return;
    size_t n = strlen(src);
    if (n > dst_size - 1) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

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
        copy_string(str_val, sizeof(str_val), s);
    }

    // 128 バイトの str_val は文字列型のときだけ意味を持つ。数値のコピーで
    // 未使用のバッファまで複製しないよう、型に応じて必要分だけ写す。
    // 式評価は Value を値渡しで何度も複製するため、ここが速度に効く。
    Value(const Value& o) : type(o.type), num_val(o.num_val), int_val(o.int_val) {
        if (o.type == Type::STR) memcpy(str_val, o.str_val, sizeof(str_val));
        else str_val[0] = '\0';
    }
    Value& operator=(const Value& o) {
        type = o.type; num_val = o.num_val; int_val = o.int_val;
        if (o.type == Type::STR) memcpy(str_val, o.str_val, sizeof(str_val));
        else str_val[0] = '\0';
        return *this;
    }

    float get_num() const {
        return num_val;
    }

    // 数値（NUM / INT）かどうか。型チェックはこれを使う
    bool is_numeric() const {
        return type == Type::NUM || type == Type::INT;
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

// 論理メモリの 16bit リトルエンディアン読み書き
inline uint16_t mem_read_u16(uint16_t addr) {
    return (uint16_t)(logical_memory[addr] | (logical_memory[addr + 1] << 8));
}
inline void mem_write_u16(uint16_t addr, uint16_t v) {
    logical_memory[addr]     = (uint8_t)(v & 0xFF);
    logical_memory[addr + 1] = (uint8_t)(v >> 8);
}

// プログラム行ノードのレイアウト: [次ノードへのポインタ(2)][行番号(2)][トークン列...]
inline uint16_t prog_next_ptr(uint16_t node) { return mem_read_u16(node); }
inline uint16_t prog_line_no(uint16_t node)  { return mem_read_u16((uint16_t)(node + 2)); }

extern uint16_t string_heap_ptr;
extern uint16_t array_heap_inner_ptr;
extern float last_rnd_val;

extern int current_line;
extern bool branch_taken;
// 分岐後に行内のどの位置から実行を再開するか。-1 は行頭（pos 0）から。
// RETURN / NEXT / UNTIL が「同じ行の続き」に戻るために使う（文単位の復帰）。
extern int branch_resume_pos;
extern bool trace_enabled; // TRON/TROFF（実行行トレース）

// CONT（STOP / Ctrl-C 後の再開）
extern int  cont_line;
extern int  cont_pos;
extern bool cont_valid;

// ON ERROR GOTO / RESUME / ERR / ERL
extern int  err_code;            // 直近のエラーコード（ERR）
extern int  err_line;            // エラーが起きた行（ERL）
extern int  error_handler_line;  // ON ERROR GOTO の飛び先（0=無効）
extern bool in_error_handler;    // ハンドラ実行中（RESUME で解除）

// WHILE / WEND
#define MAX_WHILE_STACK 8
extern int while_stack_line[MAX_WHILE_STACK];
extern int while_stack_pos[MAX_WHILE_STACK];
extern int while_stack_ptr;

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

// DEF FN ユーザー定義関数。本体は再 lex するためソース文字列で保持する。
#define MAX_USER_FUNCS 16
struct UserFunc {
    char name[MAX_TOKEN_LEN];   // 例: "FNSQ"（大文字）
    char param[MAX_TOKEN_LEN];  // 仮引数名（例: "X"）
    char body[192];             // 本体式のソース（例: "X * X"）
};
extern UserFunc user_funcs[MAX_USER_FUNCS];
extern int user_func_count;

bool  is_user_func(const char* name);
Value call_user_func(const char* name, const Value& arg);

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
void invalidate_var_hash(); // 変数表を一括変更したら索引を無効化
ArrayRef* get_array(const char* name);
void write_heap_value(uint16_t addr, const Value& val);
Value read_heap_value(uint16_t addr);

int flatten_array_index(const ArrayRef* arr, int i, int j = -1);
uint16_t find_program_line(int line_number);
uint16_t get_next_program_line(int line_number);

// ユーザー座標系（WINDOW）→ 画面座標への変換（statement_graphics_io.cpp）。
// WINDOW 未設定時は切り捨てて int 化するだけ
void user_to_screen(float ux, float uy, int& out_x, int& out_y);

// 変数名の直後の `(添字[, 添字2])` を読む（READ / INPUT / INPUT# / 代入で共用）
void parse_optional_indices(const TokenList& tokens, int& pos, int& arr_idx, int& arr_idx2);

// シーケンシャルファイル I/O（statement_graphics_io.cpp）。番号は #1〜#4
#define MAX_BASIC_FILES 4
void execute_open(const TokenList& tokens, int& pos);
void execute_close(const TokenList& tokens, int& pos);
void execute_print_file(const TokenList& tokens, int& pos);  // PRINT #n, ...
void execute_input_file(const TokenList& tokens, int& pos);  // INPUT #n, ...
void execute_line_input(const TokenList& tokens, int& pos);  // LINE INPUT [#n,] A$
int  basic_file_eof(int fileno);   // EOF(n): 1=終端 / 0=まだある
void basic_files_close_all();      // NEW / RUN 開始 / END で全ファイルを閉じる

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
void execute_while(const TokenList& tokens, int& pos);
void execute_wend(const TokenList& tokens, int& pos);
void execute_resume(const TokenList& tokens, int& pos);
void execute_def(const TokenList& tokens, int& pos);
void execute_delete(const TokenList& tokens, int& pos);
void execute_poke(const TokenList& tokens, int& pos);
void execute_get(const TokenList& tokens, int& pos);
void execute_on(const TokenList& tokens, int& pos);
void execute_mid_statement(const TokenList& tokens, int& pos);
void execute_battery_status(const TokenList& tokens, int& pos);
void execute_imu_status(const TokenList& tokens, int& pos);
void execute_rtc_status(const TokenList& tokens, int& pos);
void execute_rtc_set(const TokenList& tokens, int& pos);
void execute_randomize(const TokenList& tokens, int& pos);
void execute_sync(const TokenList& tokens, int& pos);
void execute_poweroff(const TokenList& tokens, int& pos);
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
void list_program(int from_line, int to_line); // 既定引数は parser.h 側
void renum_program(int newstart, int step);
void store_line(int line_number, const TokenList& tokens);


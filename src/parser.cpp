#include "parser.h"
#include "parser_internal.h"
#include "hal_display.h"
#include "hal_sound.h"
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------
// Interpreter State
// ---------------------------------------------------------
float last_rnd_val = 0.5f;

int current_line = -1;
bool branch_taken = false;
int branch_resume_pos = -1;

uint16_t current_color_565 = 0xFFFF; // Default White
const uint16_t PALETTE[16] = {
    0x0000, // 0: Black
    0x001F, // 1: Blue
    0x07E0, // 2: Green
    0x07FF, // 3: Cyan
    0xF800, // 4: Red
    0xF81F, // 5: Magenta
    0xFBE0, // 6: Brown
    0xC618, // 7: Light Gray
    0x7BEF, // 8: Dark Gray
    0x55FF, // 9: Light Blue
    0x5FE0, // 10: Light Green
    0x5FFF, // 11: Light Cyan
    0xFF80, // 12: Light Red
    0xFFDF, // 13: Light Magenta
    0xFFE0, // 14: Yellow
    0xFFFF  // 15: White
};

ForLoopContext for_stack[MAX_FOR_STACK];
int for_stack_ptr = 0;

int repeat_stack_line[MAX_REPEAT_STACK];
int repeat_stack_pos[MAX_REPEAT_STACK];
int repeat_stack_ptr = 0;

int call_stack[MAX_CALL_STACK];
int call_stack_pos[MAX_CALL_STACK];
int call_stack_ptr = 0;

Value data_buffer[MAX_DATA_BUFFER];
int data_buffer_size = 0;
int data_ptr = 0;

LabelEntry label_table[MAX_LABELS];
int label_table_size = 0;

// AUTO 行番号生成モードの保留状態。parse_and_execute が立て、repl が消費する。
static bool g_auto_pending = false;
static int  g_auto_start = 10;
static int  g_auto_step  = 10;

bool auto_mode_requested(int* start, int* step) {
    if (!g_auto_pending) return false;
    g_auto_pending = false;
    if (start) *start = g_auto_start;
    if (step)  *step  = g_auto_step;
    return true;
}

int resolve_label(const char* name) {
    for (int i = 0; i < label_table_size; i++) {
        if (strcmp(label_table[i].name, name) == 0) return label_table[i].line;
    }
    return -1;
}

// ---------------------------------------------------------
// Error reporting
// ---------------------------------------------------------
// エラーメッセージから Hu-BASIC のエラーコード（MANUAL の一覧）を推定する。
// 0 を返したら該当コードなし（メッセージ本文だけを表示する）。
// メッセージの分類ではなくキーワード照合なので、順序に依存する点に注意。
static int basic_error_code(const char* msg) {
    if (strstr(msg, "Out of Memory") || strstr(msg, "Heap Overflow") ||
        strstr(msg, "Stack Overflow") || strstr(msg, "Program too large")) return 7;   // Out of memory
    if (strstr(msg, "Syntax Error") || strstr(msg, "Syntax error") ||
        strstr(msg, "parenthesis") || strstr(msg, "Expected") ||
        strstr(msg, "Unrecognized")) return 2;                                           // Syntax error
    if (strstr(msg, "Type Mismatch") || strstr(msg, "Type mismatch")) return 13;        // Type mismatch
    if (strstr(msg, "Duplicate definition")) return 10;                                 // Duplicate definition
    if (strstr(msg, "target line not found") || strstr(msg, "No line after") ||
        strstr(msg, "Undefined line")) return 8;                                        // Undefined line number
    if (strstr(msg, "out of bounds") || strstr(msg, "Subscript") ||
        strstr(msg, "Array index")) return 9;                                           // Subscript out of range
    if (strstr(msg, "File Error") || strstr(msg, "File not found")) return 26;          // File not found
    if (strstr(msg, "not yet implemented") || strstr(msg, "Reserved")) return 34;       // Reserved feature
    if (strstr(msg, "Illegal function call") || strstr(msg, "Invalid color") ||
        strstr(msg, "TOUCH argument") || strstr(msg, "supports")) return 5;             // Illegal function call
    return 0;
}

// 実行時エラーを表示する。line < 0 はダイレクトモード（行番号なし）。
static void report_error(const char* what, int line) {
    int code = basic_error_code(what);
    char buf[192];
    if (line >= 0) {
        if (code > 0) snprintf(buf, sizeof(buf), "Error %d in line %d: %s\n", code, line, what);
        else          snprintf(buf, sizeof(buf), "Error in line %d: %s\n", line, what);
    } else {
        if (code > 0) snprintf(buf, sizeof(buf), "Error %d: %s\n", code, what);
        else          snprintf(buf, sizeof(buf), "%s\n", what);
    }
    basic_print(buf);
}

// ---------------------------------------------------------
// Public API
// ---------------------------------------------------------
bool parse_and_execute(const TokenList& tokens) {
    if (tokens.size == 0 || tokens.tokens[0].type == TokenType::END_OF_FILE) return false;
    
    try {
        if (tokens.tokens[0].type == TokenType::NUMBER) {
            int line_num = atoi(tokens.tokens[0].text);
            TokenList remainder;
            int j = 0;
            for (int i = 1; i < tokens.size; i++) remainder.tokens[j++] = tokens.tokens[i];
            remainder.size = j;
            store_line(line_num, remainder);
            return true;
        } else if (tokens.tokens[0].type == TokenType::NEW) {
            hal_sound_stop(); // 再生中の演奏も止める
            basic_files_close_all();
            clear_program();
            return false;
        } else if (tokens.tokens[0].type == TokenType::LIST) {
            list_program();
            return false;
        } else if (tokens.tokens[0].type == TokenType::RUN) {
            run_program();
            return false;
        } else if (tokens.tokens[0].type == TokenType::SAVE) {
            int p = 0; execute_save(tokens, p);
            return false;
        } else if (tokens.tokens[0].type == TokenType::LOAD) {
            int p = 0; execute_load(tokens, p);
            return false;
        } else if (tokens.tokens[0].type == TokenType::FILES) {
            int p = 0; execute_files(tokens, p);
            return false;
        } else if (tokens.tokens[0].type == TokenType::AUTO) {
            // AUTO [開始番号 [, 刻み]] — 行番号自動生成モードを要求する。
            // 実際に行番号を出すのは対話入力を持つ repl 側（auto_mode_requested）。
            int start = 10, step = 10;
            int p = 1;
            if (p < tokens.size && tokens.tokens[p].type == TokenType::NUMBER) {
                start = atoi(tokens.tokens[p].text); p++;
            }
            if (p < tokens.size && tokens.tokens[p].type == TokenType::COMMA) {
                p++;
                if (p < tokens.size && tokens.tokens[p].type == TokenType::NUMBER) {
                    step = atoi(tokens.tokens[p].text); p++;
                }
            }
            if (start < 0) start = 0;
            if (step <= 0) step = 10; // 刻み 0 や負値は既定に戻す（無限ループ回避）
            g_auto_pending = true;
            g_auto_start = start;
            g_auto_step = step;
            return false;
        }
        int pos = 0;
        branch_taken = false;
        while (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE) {
            execute_statement(tokens, pos);
            if (branch_taken) break;
            if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COLON) {
                pos++;
            } else break;
        }
    } catch (const std::exception& e) {
        report_error(e.what(), -1); // ダイレクトモード（行番号なし）
    }
    return false;
}

void run_program(int max_steps) {
    // RUN のたびに変数と配列を初期化する。
    // これをしないと DIM が実行のたびに配列ヒープを食い潰し、
    // 2 回目の RUN が Out of Memory になる
    execute_clear();

    for_stack_ptr = 0;
    repeat_stack_ptr = 0;
    call_stack_ptr = 0;

    // Pre-scan DATA statements and line labels
    data_buffer_size = 0;
    data_ptr = 0;
    label_table_size = 0;

    uint16_t ptr = MEMORY_TEXT_BASE;
    while (true) {
        if (logical_memory[ptr+2] == 0 && logical_memory[ptr+3] == 0 && ptr != MEMORY_TEXT_BASE) break;
        int line_no = prog_line_no(ptr);
        TokenList tokens = get_detokenized_line(ptr);

        // 行頭のラベル定義（`100 *LOOP ...`）を表に登録する
        if (tokens.size > 0 && tokens.tokens[0].type == TokenType::LABEL &&
            label_table_size < MAX_LABELS) {
            // 同名ラベルは最初の定義を優先（重複は無視）
            if (resolve_label(tokens.tokens[0].text) < 0) {
                strncpy(label_table[label_table_size].name, tokens.tokens[0].text, MAX_TOKEN_LEN - 1);
                label_table[label_table_size].name[MAX_TOKEN_LEN - 1] = '\0';
                label_table[label_table_size].line = line_no;
                label_table_size++;
            }
        }

        if (tokens.size > 0 && tokens.tokens[0].type == TokenType::DATA) {
            int pos = 1;
            while (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE) {
                if (data_buffer_size < MAX_DATA_BUFFER) {
                    data_buffer[data_buffer_size++] = parse_relation(tokens, pos);
                }
                if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
                else if (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE) break;
            }
        }
        uint16_t next_ptr = prog_next_ptr(ptr);
        if (next_ptr == 0) break;
        ptr = next_ptr;
    }
    
    // Interpreter loop
    ptr = MEMORY_TEXT_BASE;
    if (logical_memory[ptr+2] == 0 && logical_memory[ptr+3] == 0 && ptr != MEMORY_TEXT_BASE) return;
    current_line = prog_line_no(ptr);
    int steps = 0;
    int resume_pos = -1; // 次の行を行頭ではなく途中から始める場合の位置（-1 は行頭）

    while (current_line != -1 && (max_steps == -1 || steps < max_steps)) {
        // Ctrl-C による中断。無限ループから抜ける唯一の手段なので、
        // 反応が鈍らないよう十分短い間隔で確認する
        if ((steps & 0x0F) == 0 && hal_system_break_requested()) {
            hal_sound_stop(); // 非同期で鳴っている演奏も止める
            char buf[64];
            snprintf(buf, sizeof(buf), "Break in %d\n", current_line);
            basic_print(buf);
            break;
        }

        uint16_t line_ptr = find_program_line(current_line);
        if (line_ptr == 0xFFFF) break;
        
        TokenList tokens = get_detokenized_line(line_ptr);
        int pos = 0;
        // 直前の分岐が「行の途中」への復帰（RETURN / NEXT / UNTIL）を要求していたら
        // その位置から再開する。復帰位置は文の区切り（`:`）を指すので 1 つ読み飛ばす。
        if (resume_pos >= 0) {
            pos = resume_pos;
            if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COLON) pos++;
        }
        resume_pos = -1;
        branch_taken = false;
        branch_resume_pos = -1;

        try {
            while (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE) {
                execute_statement(tokens, pos);
                if (branch_taken) break;
                if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COLON) {
                    pos++;
                } else break;
            }
        } catch (const std::exception& e) {
            report_error(e.what(), current_line);
            break;
        }

        if (branch_taken) {
            // 制御構文が行内復帰位置を指定していれば次の行でそこから再開する
            resume_pos = branch_resume_pos;
        } else {
            uint16_t next_ptr = prog_next_ptr(line_ptr);
            if (next_ptr == 0 || (logical_memory[next_ptr+2] == 0 && logical_memory[next_ptr+3] == 0)) {
                current_line = -1;
            } else {
                current_line = prog_line_no(next_ptr);
            }
        }
        steps++;
    }
}

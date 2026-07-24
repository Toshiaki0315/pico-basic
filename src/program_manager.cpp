#include "parser_internal.h"
#include "hal_display.h"
#include <cstring>
#include <stdexcept>
#include <cstdlib>

// ---------------------------------------------------------
// Token serialization helpers
// ---------------------------------------------------------

const char* token_type_to_string(TokenType type) {
    switch (type) {
        case TokenType::PRINT: return "PRINT";
        case TokenType::LET: return "LET";
        case TokenType::ASSIGN: return "=";
        case TokenType::PLUS: return "+";
        case TokenType::MINUS: return "-";
        case TokenType::MUL: return "*";
        case TokenType::DIV: return "/";
        case TokenType::POWER: return "^";
        case TokenType::LPAREN: return "(";
        case TokenType::RPAREN: return ")";
        case TokenType::GOTO: return "GOTO";
        case TokenType::GOSUB: return "GOSUB";
        case TokenType::RETURN: return "RETURN";
        case TokenType::IF: return "IF";
        case TokenType::THEN: return "THEN";
        case TokenType::ELSE: return "ELSE";
        case TokenType::ELSEIF: return "ELSEIF";
        case TokenType::REM: return "REM";
        case TokenType::AND: return "AND";
        case TokenType::OR: return "OR";
        case TokenType::NOT: return "NOT";
        case TokenType::XOR: return "XOR";
        case TokenType::MOD_OP: return "MOD";
        case TokenType::INTDIV: return "\\";
        case TokenType::DEF: return "DEF";
        case TokenType::POKE: return "POKE";
        case TokenType::FOR: return "FOR";
        case TokenType::TO: return "TO";
        case TokenType::STEP: return "STEP";
        case TokenType::NEXT: return "NEXT";
        case TokenType::NEW: return "NEW";
        case TokenType::LIST: return "LIST";
        case TokenType::RUN: return "RUN";
        case TokenType::READ: return "READ";
        case TokenType::DATA: return "DATA";
        case TokenType::RESTORE: return "RESTORE";
        case TokenType::DIM: return "DIM";
        case TokenType::INPUT: return "INPUT";
        case TokenType::END: return "END";
        case TokenType::STOP: return "STOP";
        case TokenType::INIT: return "INIT";
        case TokenType::CLEAR: return "CLEAR";
        case TokenType::NEWON: return "NEWON";
        case TokenType::WIDTH: return "WIDTH";
        case TokenType::CONSOLE: return "CONSOLE";
        case TokenType::CLS: return "CLS";
        case TokenType::AUTO: return "AUTO";
        case TokenType::REPEAT: return "REPEAT";
        case TokenType::UNTIL: return "UNTIL";
        case TokenType::GET: return "GET";
        case TokenType::FILES: return "FILES";
        case TokenType::SAVE: return "SAVE";
        case TokenType::OPEN: return "OPEN";
        case TokenType::CLOSE: return "CLOSE";
        case TokenType::HASH: return "#";
        case TokenType::RENUM: return "RENUM";
        case TokenType::DELETE_CMD: return "DELETE";
        case TokenType::CONT: return "CONT";
        case TokenType::TRON: return "TRON";
        case TokenType::TROFF: return "TROFF";
        case TokenType::WHILE: return "WHILE";
        case TokenType::WEND: return "WEND";
        case TokenType::USING: return "USING";
        case TokenType::RESUME: return "RESUME";
        case TokenType::LOAD: return "LOAD";
        case TokenType::ON: return "ON";
        case TokenType::COLON: return ":";
        case TokenType::GPIO: return "GPIO";
        case TokenType::WINDOW: return "WINDOW";
        case TokenType::PSET: return "PSET";
        case TokenType::LINE: return "LINE";
        case TokenType::CIRCLE: return "CIRCLE";
        case TokenType::POLY: return "POLY";
        case TokenType::PAINT: return "PAINT";
        case TokenType::GET_AT: return "GET@";
        case TokenType::PUT_AT: return "PUT@";
        case TokenType::COLOR: return "COLOR";
        case TokenType::BEEP: return "BEEP";
        case TokenType::PLAY: return "PLAY";
        case TokenType::MUSIC: return "MUSIC";
        case TokenType::SOUND: return "SOUND";
        case TokenType::GT: return ">";
        case TokenType::LT: return "<";
        case TokenType::GTE: return ">=";
        case TokenType::LTE: return "<=";
        case TokenType::NEQ: return "<>";
        case TokenType::COMMA: return ",";
        case TokenType::SEMICOLON: return ";";
        case TokenType::WAIT: return "WAIT";
        default: return "";
    }
}

static int tokenize(const TokenList& tokens, uint8_t* buffer) {
    int ptr = 0;
    for (int i=0; i<tokens.size; i++) {
        buffer[ptr++] = (uint8_t)tokens.tokens[i].type;
        if (tokens.tokens[i].type == TokenType::NUMBER ||
            tokens.tokens[i].type == TokenType::IDENTIFIER ||
            tokens.tokens[i].type == TokenType::STRING ||
            tokens.tokens[i].type == TokenType::LABEL ||
            tokens.tokens[i].type == TokenType::REM) {
            int len = strlen(tokens.tokens[i].text);
            buffer[ptr++] = (uint8_t)len;
            memcpy(&buffer[ptr], tokens.tokens[i].text, len);
            ptr += len;
        }
    }
    buffer[ptr++] = 0xFF; // EOL byte
    return ptr;
}

static TokenList detokenize(const uint8_t* buffer) {
    TokenList t;
    int ptr = 0;
    while (t.size < MAX_TOKENS_PER_LINE && buffer[ptr] != 0xFF) {
        if (ptr > 512) break; 
        t.tokens[t.size].type = (TokenType)buffer[ptr++];
        if (t.tokens[t.size].type == TokenType::NUMBER ||
            t.tokens[t.size].type == TokenType::IDENTIFIER ||
            t.tokens[t.size].type == TokenType::STRING ||
            t.tokens[t.size].type == TokenType::LABEL ||
            t.tokens[t.size].type == TokenType::REM) {
            int len = buffer[ptr++];
            if (len > 64) len = 64; 
            memcpy(t.tokens[t.size].text, &buffer[ptr], len);
            t.tokens[t.size].text[len] = '\0';
            ptr += len;
        } else {
            const char* kw = token_type_to_string(t.tokens[t.size].type);
            strncpy(t.tokens[t.size].text, kw, MAX_TOKEN_LEN - 1);
            t.tokens[t.size].text[MAX_TOKEN_LEN - 1] = '\0';
        }
        t.size++;
    }
    return t;
}

static void update_program_links() {
    uint16_t ptr = MEMORY_TEXT_BASE;
    uint16_t first_next = prog_next_ptr(ptr);
    if (first_next == 0 && logical_memory[ptr+2] == 0 && logical_memory[ptr+3] == 0) return;

    while (ptr < MEMORY_VAR_BASE) {
        uint16_t code_ptr = ptr + 4;
        while (code_ptr < MEMORY_VAR_BASE && logical_memory[code_ptr] != 0xFF) {
            TokenType type = (TokenType)logical_memory[code_ptr++];
            if (type == TokenType::NUMBER || type == TokenType::IDENTIFIER ||
                type == TokenType::STRING || type == TokenType::LABEL ||
                type == TokenType::REM) {
                if (code_ptr < MEMORY_VAR_BASE) {
                    int len = logical_memory[code_ptr++];
                    code_ptr += len;
                }
            }
        }
        if (code_ptr >= MEMORY_VAR_BASE) break;
        code_ptr++; 
        
        uint16_t actual_next = code_ptr;
        mem_write_u16(ptr, actual_next);

        if (actual_next + 4 >= MEMORY_VAR_BASE || 
            (logical_memory[actual_next] == 0 && logical_memory[actual_next+1] == 0 &&
             logical_memory[actual_next+2] == 0 && logical_memory[actual_next+3] == 0)) {
            logical_memory[actual_next] = 0;
            logical_memory[actual_next+1] = 0;
            break;
        }
        ptr = actual_next;
    }
}

uint16_t find_program_line(int line_number) {
    uint16_t ptr = MEMORY_TEXT_BASE;
    if (logical_memory[ptr] == 0 && logical_memory[ptr+1] == 0 && 
        logical_memory[ptr+2] == 0 && logical_memory[ptr+3] == 0) return 0xFFFF;

    while (true) {
        if (logical_memory[ptr+2] == 0 && logical_memory[ptr+3] == 0 && ptr != MEMORY_TEXT_BASE) return 0xFFFF;
        uint16_t current_line = prog_line_no(ptr);
        if (current_line == line_number) return ptr;
        uint16_t next_ptr = prog_next_ptr(ptr);
        if (next_ptr == 0) return 0xFFFF;
        ptr = next_ptr;
    }
}

uint16_t get_next_program_line(int line_number) {
    uint16_t ptr = MEMORY_TEXT_BASE;
    while (true) {
        uint16_t next_ptr = prog_next_ptr(ptr);
        if (next_ptr == 0) return 0xFFFF;
        uint16_t current_line = prog_line_no(ptr);
        if (current_line > line_number) return ptr;
        ptr = next_ptr;
    }
}

static uint16_t get_end_of_text() {
    uint16_t ptr = MEMORY_TEXT_BASE;
    while (true) {
        uint16_t next_ptr = prog_next_ptr(ptr);
        if (next_ptr == 0) return ptr;
        ptr = next_ptr;
    }
}

void store_line(int line_number, const TokenList& tokens) {
    cont_valid = false; // プログラムを編集したら再開位置は信用できない
    uint16_t ptr = find_program_line(line_number);
    uint16_t end_ptr = get_end_of_text() + 2; 
    
    if (ptr != 0xFFFF) {
        uint16_t next_ptr = prog_next_ptr(ptr);
        memmove(&logical_memory[ptr], &logical_memory[next_ptr], end_ptr - next_ptr);
        end_ptr -= (next_ptr - ptr);
        if (end_ptr + 2 < MEMORY_VAR_BASE) {
            logical_memory[end_ptr] = 0;
            logical_memory[end_ptr+1] = 0;
        }
        update_program_links();
    }

    if (tokens.size == 0 || (tokens.size == 1 && tokens.tokens[0].type == TokenType::END_OF_FILE)) return;

    uint16_t insert_ptr = MEMORY_TEXT_BASE;
    while (true) {
        uint16_t next_ptr = prog_next_ptr(insert_ptr);
        if (next_ptr == 0) break;
        uint16_t current_line = prog_line_no(insert_ptr);
        if (current_line > line_number) break;
        insert_ptr = next_ptr;
    }
    
    uint8_t buffer[256];
    int code_len = tokenize(tokens, buffer);
    int total_len = 4 + code_len;
    
    if (end_ptr + total_len >= MEMORY_VAR_BASE) {
        throw std::runtime_error("Out of Memory: Program too large");
    }
    
    memmove(&logical_memory[insert_ptr + total_len], &logical_memory[insert_ptr], end_ptr - insert_ptr);
    mem_write_u16((uint16_t)(insert_ptr + 2), (uint16_t)line_number);
    memcpy(&logical_memory[insert_ptr+4], buffer, code_len);
    
    uint16_t new_end = end_ptr + total_len;
    if (new_end + 2 < MEMORY_VAR_BASE) {
        logical_memory[new_end] = 0;
        logical_memory[new_end+1] = 0;
    }

    update_program_links();
}

void clear_program() {
    cont_valid = false;
    memset(logical_memory, 0, 8);
    memset(&logical_memory[MEMORY_VAR_BASE], 0, VAR_TABLE_SIZE + ARRAY_TABLE_SIZE);
    
    string_heap_ptr = STRING_HEAP_BASE;
    array_heap_inner_ptr = DATA_HEAP_BASE;
    
    for_stack_ptr = 0;
    call_stack_ptr = 0;
    data_buffer_size = 0;
    data_ptr = 0;
}

void list_program(int from_line, int to_line) {
    uint16_t ptr = MEMORY_TEXT_BASE;
    if (logical_memory[ptr] == 0 && logical_memory[ptr+1] == 0 && 
        logical_memory[ptr+2] == 0 && logical_memory[ptr+3] == 0) return;

    char buffer[1024];
    while (ptr < MEMORY_VAR_BASE) {
        if (logical_memory[ptr+2] == 0 && logical_memory[ptr+3] == 0 && ptr != MEMORY_TEXT_BASE) break;
        
        uint16_t line_num = prog_line_no(ptr);
        if (line_num < from_line || line_num > to_line) { // 範囲外はスキップ
            uint16_t skip_next = prog_next_ptr(ptr);
            if (skip_next == 0) break;
            ptr = skip_next;
            continue;
        }
        TokenList tokens = detokenize(&logical_memory[ptr+4]);
        
        int bpos = snprintf(buffer, sizeof(buffer), "%d", line_num);
        for (int i = 0; i < tokens.size; i++) {
            if (tokens.tokens[i].type == TokenType::END_OF_FILE) break;
            bpos += snprintf(buffer + bpos, sizeof(buffer) - bpos, " ");
            if (tokens.tokens[i].type == TokenType::STRING) {
                bpos += snprintf(buffer + bpos, sizeof(buffer) - bpos, "\"%s\"", tokens.tokens[i].text);
            } else if (tokens.tokens[i].type == TokenType::REM) {
                bpos += snprintf(buffer + bpos, sizeof(buffer) - bpos, "REM %s", tokens.tokens[i].text);
            } else {
                bpos += snprintf(buffer + bpos, sizeof(buffer) - bpos, "%s", tokens.tokens[i].text);
            }
        }
        snprintf(buffer + bpos, sizeof(buffer) - bpos, "\n");
        basic_print(buffer);
        
        uint16_t next_ptr = prog_next_ptr(ptr);
        if (next_ptr == 0) break;
        ptr = next_ptr;
    }
}

// ---------------------------------------------------------
// Added for detokenize use in run_program
// ---------------------------------------------------------
TokenList get_detokenized_line(uint16_t line_ptr) {
    return detokenize(&logical_memory[line_ptr+4]);
}

// ---------------------------------------------------------
// RENUM: 行番号を newstart から step 刻みで振り直す。
// GOTO / GOSUB / THEN / ELSE（および ON ... のカンマ区切りリスト）の
// 飛び先 NUMBER も対応表で書き換える。行ラベルは名前なので影響しない。
// ---------------------------------------------------------
void renum_program(int newstart, int step) {
    // 1) 旧→新の対応表を作る
    static uint16_t old_no[MAX_PROGRAM_LINES];
    int count = 0;
    uint16_t ptr = MEMORY_TEXT_BASE;
    if (logical_memory[ptr] == 0 && logical_memory[ptr+1] == 0 &&
        logical_memory[ptr+2] == 0 && logical_memory[ptr+3] == 0) return; // 空
    while (ptr < MEMORY_VAR_BASE && count < MAX_PROGRAM_LINES) {
        if (prog_line_no(ptr) == 0 && ptr != MEMORY_TEXT_BASE) break;
        old_no[count++] = prog_line_no(ptr);
        uint16_t next = prog_next_ptr(ptr);
        if (next == 0) break;
        ptr = next;
    }
    long last = (long)newstart + (long)(count - 1) * step;
    if (last > 65535) throw std::runtime_error("RENUM: line number overflow");

    auto map_line = [&](int old_line) -> int {
        for (int i = 0; i < count; i++)
            if (old_no[i] == old_line) return newstart + i * step;
        return -1; // 存在しない飛び先はそのまま残す
    };

    // 2) 飛び先 NUMBER を書き換える（行番号自体はまだ変えないので反復は安定）
    for (int i = 0; i < count; i++) {
        uint16_t lp = find_program_line(old_no[i]);
        if (lp == 0xFFFF) continue;
        TokenList t = get_detokenized_line(lp);
        bool changed = false;
        bool target_mode = false; // GOTO/GOSUB/THEN/ELSE の直後の数値列
        for (int k = 0; k < t.size; k++) {
            TokenType ty = t.tokens[k].type;
            if (ty == TokenType::GOTO || ty == TokenType::GOSUB ||
                ty == TokenType::THEN || ty == TokenType::ELSE) {
                target_mode = true;
                continue;
            }
            if (!target_mode) continue;
            if (ty == TokenType::NUMBER) {
                int mapped = map_line(atoi(t.tokens[k].text));
                if (mapped >= 0) {
                    snprintf(t.tokens[k].text, MAX_TOKEN_LEN, "%d", mapped);
                    changed = true;
                }
            } else if (ty != TokenType::COMMA) { // カンマ区切り（ON ...）は継続
                target_mode = false;
            }
        }
        if (changed) store_line(old_no[i], t);
    }

    // 3) 行番号フィールドを一斉に書き換える（並び順は保たれる）
    ptr = MEMORY_TEXT_BASE;
    for (int i = 0; i < count && ptr < MEMORY_VAR_BASE; i++) {
        mem_write_u16((uint16_t)(ptr + 2), (uint16_t)(newstart + i * step));
        uint16_t next = prog_next_ptr(ptr);
        if (next == 0) break;
        ptr = next;
    }
}

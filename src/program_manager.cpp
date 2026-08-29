#include "parser_internal.h"
#include <cstdarg>
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
        case TokenType::RANDOMIZE: return "RANDOMIZE";
        case TokenType::SYNC: return "SYNC";
        case TokenType::POWEROFF: return "POWEROFF";
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

// トークンが本文を持つ種別か。中間コードでは「型 1 + 長さ 1 + 本文」に展開される
static bool token_carries_text(TokenType type) {
    return type == TokenType::NUMBER || type == TokenType::IDENTIFIER ||
           type == TokenType::STRING || type == TokenType::LABEL ||
           type == TokenType::REM;
}

// トークン列を中間コードに変換する。収まらなければ -1。
//
// 以前は書き込み量を確かめずに 256 バイトのスタック配列へ書いていた。
// 本文を持つトークンは本文に加えて 2 バイト使うので、短いトークンが並ぶほど
// 中間コードは元の行より長くなる。`10 1 1 1 ...` と数値を 90 個並べた
// 254 文字の行（REPL の上限は 255 文字）で、実際にスタックを踏み抜いていた。
static int tokenize(const TokenList& tokens, uint8_t* buffer, int buffer_size) {
    int ptr = 0;
    for (int i=0; i<tokens.size; i++) {
        int len = token_carries_text(tokens.tokens[i].type)
                    ? (int)strlen(tokens.tokens[i].text) : 0;
        int need = 1 + (len > 0 || token_carries_text(tokens.tokens[i].type) ? 1 + len : 0);
        if (ptr + need + 1 > buffer_size) return -1; // +1 は行末の 0xFF

        buffer[ptr++] = (uint8_t)tokens.tokens[i].type;
        if (token_carries_text(tokens.tokens[i].type)) {
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
        if (ptr >= MAX_LINE_CODE_LEN) break;
        t.tokens[t.size].type = (TokenType)buffer[ptr++];
        if (token_carries_text(t.tokens[t.size].type)) {
            int stored = buffer[ptr++];
            // 読み出しは Token の本文に収まる分だけ。ただし **進む量は格納された
            // 長さそのもの**にすること。切り詰めた長さで進めると、はみ出した
            // バイトが次のトークンとして読まれて行全体が化ける。
            // 以前は 64 で切り詰めたうえに切り詰めた長さで進めていたため、
            // 64 文字を超える文字列や REM を含む行は格納すると壊れていた
            int len = (stored > MAX_TOKEN_LEN - 1) ? MAX_TOKEN_LEN - 1 : stored;
            memcpy(t.tokens[t.size].text, &buffer[ptr], len);
            t.tokens[t.size].text[len] = '\0';
            ptr += stored;
        } else {
            const char* kw = token_type_to_string(t.tokens[t.size].type);
            copy_string(t.tokens[t.size].text, MAX_TOKEN_LEN, kw);
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

// ---------------------------------------------------------
// 行番号 → ノード位置の索引キャッシュ。
//
// 実行ループは 1 行実行するたびに find_program_line を呼ぶ。連結リストを
// 頭から歩くと長いプログラムのループが O(n^2) になるため、初回参照時に
// 索引を作って二分探索する。プログラムを書き換える操作（store_line /
// clear_program / renum_program）で無効化し、次の参照で作り直す。
// ---------------------------------------------------------
static uint16_t line_index_no[MAX_PROGRAM_LINES];
static uint16_t line_index_ptr[MAX_PROGRAM_LINES];
static int line_index_count = -1; // -1 = 無効

static void invalidate_line_index() { line_index_count = -1; }

static void build_line_index() {
    line_index_count = 0;
    uint16_t ptr = MEMORY_TEXT_BASE;
    if (logical_memory[ptr] == 0 && logical_memory[ptr+1] == 0 &&
        logical_memory[ptr+2] == 0 && logical_memory[ptr+3] == 0) return;
    while (ptr < MEMORY_VAR_BASE && line_index_count < MAX_PROGRAM_LINES) {
        if (prog_line_no(ptr) == 0 && ptr != MEMORY_TEXT_BASE) break;
        line_index_no[line_index_count]  = prog_line_no(ptr);
        line_index_ptr[line_index_count] = ptr;
        line_index_count++;
        uint16_t next = prog_next_ptr(ptr);
        if (next == 0) break;
        ptr = next;
    }
}

uint16_t find_program_line(int line_number) {
    if (line_index_count < 0) build_line_index();
    int lo = 0, hi = line_index_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (line_index_no[mid] == line_number) return line_index_ptr[mid];
        if (line_index_no[mid] < line_number) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0xFFFF;
}

uint16_t get_next_program_line(int line_number) {
    if (line_index_count < 0) build_line_index();
    // line_number より大きい最初の行を探す
    int lo = 0, hi = line_index_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (line_index_no[mid] <= line_number) lo = mid + 1;
        else hi = mid;
    }
    return (lo < line_index_count) ? line_index_ptr[lo] : 0xFFFF;
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
    invalidate_line_index();
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
        invalidate_line_index(); // 既存行を消したので索引は古い
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
    
    uint8_t buffer[MAX_LINE_CODE_LEN];
    int code_len = tokenize(tokens, buffer, sizeof(buffer));
    if (code_len < 0) throw std::runtime_error("Line too long");
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
    invalidate_line_index(); // 挿入で配置が変わった
}

void clear_program() {
    cont_valid = false;
    invalidate_line_index();
    memset(logical_memory, 0, 8);
    memset(&logical_memory[MEMORY_VAR_BASE], 0, VAR_TABLE_SIZE + ARRAY_TABLE_SIZE);
    
    string_heap_ptr = STRING_HEAP_BASE;
    array_heap_inner_ptr = DATA_HEAP_BASE;
    
    for_stack_ptr = 0;
    call_stack_ptr = 0;
    data_buffer_size = 0;
    data_ptr = 0;
}

// buffer の末尾へ書き足す。pos は「実際に書けた長さ」だけ進む。
//
// snprintf の戻り値を積算する書き方だと、切り詰めが起きた時点で pos が
// バッファ長を超え、以降の書き込み位置と残り長がどちらも壊れる。
static void append_to(char* buffer, size_t size, size_t& pos, const char* fmt, ...) {
    if (pos + 1 >= size) return; // 残りが終端ぶんしか無い
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buffer + pos, size - pos, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    pos += ((size_t)n < size - pos) ? (size_t)n : (size - pos - 1);
}

// トークン 1 個をソースの見た目に戻す。戻り値は書いたバイト数。
//
// LIST と SAVE の両方がこれを使う。別々に書くと、text に本文しか入っていない
// トークンの扱いが片方だけ抜ける。実際 SAVE には REM の分岐が無く、
// `10 REM --- TITLE ---` が `10 - - - TITLE - - -` として保存されていた。
int token_to_source(char* out, size_t out_size, const Token& t) {
    switch (t.type) {
        case TokenType::STRING:
            // 引用符は text に入っていないので書き戻す
            return snprintf(out, out_size, "\"%s\"", t.text);
        case TokenType::REM:
            // text はコメント本文だけ。命令語は捨てられているので付け直す。
            // `'` で書かれていても REM に正規化される（字句解析では同じもの）
            return snprintf(out, out_size, "REM %s", t.text);
        default:
            return snprintf(out, out_size, "%s", t.text);
    }
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
        
        // snprintf は「切り詰めなければ書いたはずの長さ」を返すので、戻り値を
        // そのまま足していくと bpos がバッファ長を超える。超えた先では
        // buffer + bpos がバッファ外を指し、sizeof(buffer) - bpos は size_t で
        // 折り返して巨大値になるため、次の snprintf がスタックを踏み抜く。
        // 実際に書けた分だけ進めて、残りが無くなったら書くのをやめる
        size_t bpos = 0;
        append_to(buffer, sizeof(buffer), bpos, "%d", line_num);
        for (int i = 0; i < tokens.size; i++) {
            if (tokens.tokens[i].type == TokenType::END_OF_FILE) break;
            append_to(buffer, sizeof(buffer), bpos, " ");
            char rendered[MAX_TOKEN_LEN + 8];
            token_to_source(rendered, sizeof(rendered), tokens.tokens[i]);
            append_to(buffer, sizeof(buffer), bpos, "%s", rendered);
        }
        append_to(buffer, sizeof(buffer), bpos, "\n");
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
    invalidate_line_index(); // 行番号が変わったので索引を作り直す
}

#include "parser_internal.h"
#include "hal_display.h"
#include "line_input.h"
#include "kana_utf8.h"
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// 値を作る・持つ・出す文。
// PRINT / PRINT USING / 代入 / DIM / READ・DATA / INPUT / GET / MID$ 文 /
// DEF FN / POKE / CLEAR / DELETE。
//
// 制御構造は statement_control.cpp、本体の状態を触る文は statement_device.cpp、
// トークンからの振り分けは statement_dispatch.cpp にある。

void basic_print(const char* s) {
    hal_display_print(s);
    // LCD は生のバイトのまま。シリアルだけ半角カタカナを UTF-8 に直して送る
    serial_print_kana(s);
}

static void execute_print_using(const TokenList& tokens, int& pos);

void execute_print(const TokenList& tokens, int& pos) {
    pos++;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::HASH) {
        execute_print_file(tokens, pos); // PRINT #n, ...
        return;
    }
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::USING) {
        execute_print_using(tokens, pos); // PRINT USING "書式"; ...
        return;
    }
    char output[512] = "";
    bool newline = true;
    while (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE &&
           tokens.tokens[pos].type != TokenType::ELSE && tokens.tokens[pos].type != TokenType::ELSEIF &&
           tokens.tokens[pos].type != TokenType::REM &&
           tokens.tokens[pos].type != TokenType::COLON) {
        if (tokens.tokens[pos].type == TokenType::COMMA) {
            strncat(output, "\t", sizeof(output) - strlen(output) - 1);
            pos++;
            newline = false;
        } else if (tokens.tokens[pos].type == TokenType::SEMICOLON) {
            pos++;
            newline = false;
        } else if (tokens.tokens[pos].type == TokenType::IDENTIFIER && strcmp(tokens.tokens[pos].text, "TAB") == 0) {
            pos++;
            require_token(tokens, pos, TokenType::LPAREN, "Expected '(' after TAB"); pos++;
            Value v = parse_relation(tokens, pos);
            require_token(tokens, pos, TokenType::RPAREN, "Expected ')' after TAB"); pos++;
            
            basic_print(output);
            output[0] = '\0';
            hal_display_locate(static_cast<int>(v.num_val), -1);
            newline = false;
        } else {
            Value result = parse_relation(tokens, pos);
            strncat(output, result.c_str(), sizeof(output) - strlen(output) - 1);
            newline = true;
        }
    }
    if (newline) strncat(output, "\n", sizeof(output) - strlen(output) - 1);
    basic_print(output);
}

UserFunc user_funcs[MAX_USER_FUNCS];
int user_func_count = 0;

void execute_clear() {
    memset(&logical_memory[MEMORY_VAR_BASE], 0, VAR_TABLE_SIZE);
    memset(&logical_memory[ARRAY_TABLE_BASE], 0, ARRAY_TABLE_SIZE);
    string_heap_ptr = STRING_HEAP_BASE;
    array_heap_inner_ptr = DATA_HEAP_BASE;
    invalidate_var_hash(); // 変数表を消したので索引を作り直す
    user_func_count = 0; // DEF FN も初期化（プログラムは実行中に再定義する）
    basic_files_close_all(); // RUN / CLEAR のたびにファイルも初期状態へ
}

static UserFunc* find_user_func(const char* name) {
    for (int i = 0; i < user_func_count; i++)
        if (strcmp(user_funcs[i].name, name) == 0) return &user_funcs[i];
    return nullptr;
}

bool is_user_func(const char* name) {
    return find_user_func(name) != nullptr;
}

// DEF FN<名前>(<引数>) = <式>
void execute_def(const TokenList& tokens, int& pos) {
    pos++; // DEF
    if (pos >= tokens.size || tokens.tokens[pos].type != TokenType::IDENTIFIER)
        throw std::runtime_error("Syntax Error: Expected FN name after DEF");
    char fname[MAX_TOKEN_LEN];
    copy_string(fname, sizeof(fname), tokens.tokens[pos].text);
    fname[MAX_TOKEN_LEN - 1] = '\0';
    if (!(fname[0] == 'F' && fname[1] == 'N' && fname[2] != '\0'))
        throw std::runtime_error("Syntax Error: DEF name must start with FN");
    pos++;

    require_token(tokens, pos, TokenType::LPAREN, "Syntax Error: Expected '(' after FN name"); pos++;
    require_token(tokens, pos, TokenType::IDENTIFIER, "Syntax Error: Expected parameter name");
    char pname[MAX_TOKEN_LEN];
    copy_string(pname, sizeof(pname), tokens.tokens[pos].text);
    pname[MAX_TOKEN_LEN - 1] = '\0';
    pos++;
    require_token(tokens, pos, TokenType::RPAREN, "Syntax Error: Expected ')' after parameter"); pos++;
    require_token(tokens, pos, TokenType::ASSIGN, "Syntax Error: Expected '=' in DEF FN"); pos++;

    // 本体式を、行末または `:` までソース文字列として組み立てる（呼び出し時に再 lex）
    char body[192];
    int bp = 0;
    while (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE &&
           tokens.tokens[pos].type != TokenType::COLON) {
        const char* txt = tokens.tokens[pos].text;
        char piece[MAX_TOKEN_LEN + 2];
        if (tokens.tokens[pos].type == TokenType::STRING)
            snprintf(piece, sizeof(piece), "\"%s\"", txt);
        else
            snprintf(piece, sizeof(piece), "%s", txt);
        int need = (int)strlen(piece) + (bp > 0 ? 1 : 0);
        if (bp + need >= (int)sizeof(body)) throw std::runtime_error("DEF FN body too long");
        if (bp > 0) body[bp++] = ' ';
        strcpy(&body[bp], piece);
        bp += (int)strlen(piece);
        pos++;
    }
    body[bp] = '\0';
    if (bp == 0) throw std::runtime_error("Syntax Error: DEF FN has no body");

    // 既存を上書き、無ければ追加
    UserFunc* f = find_user_func(fname);
    if (!f) {
        if (user_func_count >= MAX_USER_FUNCS) throw std::runtime_error("Too many DEF FN definitions");
        f = &user_funcs[user_func_count++];
    }
    copy_string(f->name, sizeof(f->name), fname);
    copy_string(f->param, sizeof(f->param), pname);
    copy_string(f->body, sizeof(f->body), body);
}

// DELETE 開始[-終了] — 行番号の範囲を削除する（DELETE 100 は 1 行だけ）
void execute_delete(const TokenList& tokens, int& pos) {
    pos++; // DELETE
    int from = -1, to = -1;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::NUMBER) {
        from = atoi(tokens.tokens[pos].text); pos++;
    }
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::MINUS) {
        pos++;
        to = 65535;
        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::NUMBER) {
            to = atoi(tokens.tokens[pos].text); pos++;
        }
    } else if (from >= 0) {
        to = from; // DELETE 100 → その行だけ
    }
    if (from < 0 && to < 0) throw std::runtime_error("Syntax Error: DELETE needs a line range");
    if (from < 0) from = 0;

    // 消しながら歩くと壊れるので、先に対象の行番号を集める
    static uint16_t targets[MAX_PROGRAM_LINES];
    int n = 0;
    uint16_t ptr = MEMORY_TEXT_BASE;
    if (!(logical_memory[ptr] == 0 && logical_memory[ptr+1] == 0 &&
          logical_memory[ptr+2] == 0 && logical_memory[ptr+3] == 0)) {
        while (ptr < MEMORY_VAR_BASE && n < MAX_PROGRAM_LINES) {
            uint16_t ln = prog_line_no(ptr);
            if (ln == 0 && ptr != MEMORY_TEXT_BASE) break;
            if ((int)ln >= from && (int)ln <= to) targets[n++] = ln;
            uint16_t next = prog_next_ptr(ptr);
            if (next == 0) break;
            ptr = next;
        }
    }
    TokenList empty;
    for (int i = 0; i < n; i++) store_line(targets[i], empty);
}

// POKE アドレス, 値 — 論理メモリ（logical_memory）に 1 バイト書き込む。
// このアドレスは X1 の物理メモリマップではなく、本実装の 64KB 論理メモリ。
// プログラム・変数領域を上書きすると壊れる点は実機同様なので注意。
void execute_poke(const TokenList& tokens, int& pos) {
    pos++; // POKE
    Value addr_v = parse_relation(tokens, pos);
    if (!addr_v.is_numeric())
        throw std::runtime_error("Type Mismatch: POKE address must be numeric");
    require_token(tokens, pos, TokenType::COMMA, "Syntax Error: Expected ',' in POKE"); pos++;
    Value val_v = parse_relation(tokens, pos);
    if (!val_v.is_numeric())
        throw std::runtime_error("Type Mismatch: POKE value must be numeric");

    int addr = static_cast<int>(addr_v.num_val);
    if (addr < 0 || addr > 65535) throw std::runtime_error("Illegal function call: POKE address out of range");
    logical_memory[addr] = static_cast<uint8_t>(static_cast<int>(val_v.num_val) & 0xFF);
}

// FN 呼び出し: 仮引数に実引数を束縛して本体式を評価する
Value call_user_func(const char* name, const Value& arg) {
    static int depth = 0;
    UserFunc* f = find_user_func(name);
    if (!f) throw std::runtime_error("Undefined user function");
    if (depth >= 24) throw std::runtime_error("FN recursion too deep");

    // 仮引数の元の値を退避（呼び出し後に復元してスコープを守る）
    Value saved; bool had = get_variable(f->param, saved);
    set_variable(f->param, arg);

    TokenList body = lex(f->body);
    int p = 0;
    depth++;
    Value result;
    try {
        result = parse_relation(body, p);
    } catch (...) {
        depth--;
        if (had) set_variable(f->param, saved);
        throw;
    }
    depth--;
    if (had) set_variable(f->param, saved);
    return result;
}

// 変数名の直後に `(添字[, 添字2])` があれば読み取る（READ / INPUT / 代入の共通処理）。
// 添字が無ければ arr_idx = -1 のまま返す。
void parse_optional_indices(const TokenList& tokens, int& pos, int& arr_idx, int& arr_idx2) {
    arr_idx = -1;
    arr_idx2 = -1;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::LPAREN) {
        pos++;
        Value idx_val = parse_relation(tokens, pos);
        if (!idx_val.is_numeric()) throw std::runtime_error("Type Mismatch: Array index");
        arr_idx = static_cast<int>(idx_val.num_val);
        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
            pos++;
            Value idx2_val = parse_relation(tokens, pos);
            if (!idx2_val.is_numeric()) throw std::runtime_error("Type Mismatch: Array index");
            arr_idx2 = static_cast<int>(idx2_val.num_val);
        }
        require_token(tokens, pos, TokenType::RPAREN, "Syntax Error: Expected ')'");
        pos++;
    }
}

void execute_read(const TokenList& tokens, int& pos) {
    pos++; 
    while (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE) {
        require_token(tokens, pos, TokenType::IDENTIFIER, "Syntax Error: READ expects identifier");
        char var_name[64];
        copy_string(var_name, sizeof(var_name), tokens.tokens[pos].text);
        pos++;
        
        int arr_idx, arr_idx2;
        parse_optional_indices(tokens, pos, arr_idx, arr_idx2);
        
        if (data_ptr >= data_buffer_size) throw std::runtime_error("Out of DATA");
        Value val = data_buffer[data_ptr++];
        
        int nlen = strlen(var_name);
        bool is_str_var = (nlen > 0 && var_name[nlen-1] == '$');
        bool is_int_var = (nlen > 0 && var_name[nlen-1] == '%');
        if (is_str_var && val.type != Value::Type::STR) throw std::runtime_error("Type Mismatch in READ (Expected String)");
        if (!is_str_var && val.type == Value::Type::STR) throw std::runtime_error("Type Mismatch in READ (Expected Number)");
        if (is_int_var) val = Value((int)val.num_val);
        else if (!is_str_var && val.type == Value::Type::INT) val = Value(val.num_val);
        
        set_variable_at(var_name, arr_idx, arr_idx2, val);
        
        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) { pos++; } else { break; }
    }
}

void execute_dim(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::IDENTIFIER, "Syntax Error: Expected identifier after DIM");
    char var_name[64];
    copy_string(var_name, sizeof(var_name), tokens.tokens[pos].text);
    pos++;
    
    require_token(tokens, pos, TokenType::LPAREN, "Syntax Error: Expected '(' after DIM variable");
    pos++;
    Value size1_val = parse_relation(tokens, pos);
    if (!size1_val.is_numeric() || size1_val.num_val < 0)
        throw std::runtime_error("Syntax Error: Invalid Array Size");
    int dim1_count = static_cast<int>(size1_val.num_val) + 1;

    int dim2_count = 0;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
        pos++;
        Value size2_val = parse_relation(tokens, pos);
        if (!size2_val.is_numeric() || size2_val.num_val < 0)
            throw std::runtime_error("Syntax Error: Invalid Array Size");
        dim2_count = static_cast<int>(size2_val.num_val) + 1;
    }

    require_token(tokens, pos, TokenType::RPAREN, "Syntax Error: Expected ')' in DIM");
    pos++;

    int ndim           = (dim2_count > 0) ? 2 : 1;
    int arr_size_count = (ndim == 2) ? dim1_count * dim2_count : dim1_count;
    uint16_t total_bytes = (uint16_t)(arr_size_count * 8);
    if (array_heap_inner_ptr + total_bytes > STRING_HEAP_BASE)
        throw std::runtime_error("Out of Memory: Array Heap Full");

    uint16_t table_addr = 0xFFFF;
    for (int i = 0; i < MAX_VARIABLES; ++i) {
        uint16_t addr = ARRAY_TABLE_BASE + (i * 16);
        if (logical_memory[addr + 8] != 0 &&
            strncmp((const char*)&logical_memory[addr], var_name, 8) == 0) {
            table_addr = addr;
            break;
        }
    }
    if (table_addr == 0xFFFF) {
        for (int i = 0; i < MAX_VARIABLES; ++i) {
            uint16_t addr = ARRAY_TABLE_BASE + (i * 16);
            if (logical_memory[addr + 8] == 0) {
                table_addr = addr;
                break;
            }
        }
    }
    if (table_addr == 0xFFFF) throw std::runtime_error("Out of Memory: Too many arrays");

    // 同じ名前を再び DIM した場合、以前の領域を解放する術が無いまま
    // ヒープを進めてしまうと二重に消費する。Hu-BASIC と同じくエラーにする
    // （やり直したいときは CLEAR / NEW、または RUN で初期化される）
    if (logical_memory[table_addr + 8] != 0) {
        throw std::runtime_error("Duplicate definition: array already dimensioned");
    }

    uint16_t start_addr  = array_heap_inner_ptr;
    uint16_t dim1_u16    = (uint16_t)dim1_count;
    uint16_t dim2_u16    = (uint16_t)dim2_count;

    logical_memory[table_addr + 8] = 1;              
    logical_memory[table_addr + 9] = (uint8_t)ndim;  
    // 配列表の名前欄は 8 バイト固定長。終端は持たない
    copy_fixed_field(&logical_memory[table_addr], 8, var_name);
    memcpy(&logical_memory[table_addr + 10], &dim1_u16,    2);
    memcpy(&logical_memory[table_addr + 12], &dim2_u16,    2);
    memcpy(&logical_memory[table_addr + 14], &start_addr,  2);

    array_heap_inner_ptr += total_bytes;
    memset(&logical_memory[start_addr], 0, total_bytes);

    int nlen = strlen(var_name);
    Value default_val = (nlen > 0 && var_name[nlen-1] == '$') ? Value("") : Value(0.0f);
    for (int i = 0; i < arr_size_count; i++)
        write_heap_value(start_addr + (i * 8), default_val);
}

void execute_assignment(const TokenList& tokens, int& pos, bool explicit_let) {
    if (explicit_let) {
        pos++;
        require_token(tokens, pos, TokenType::IDENTIFIER, "Syntax Error: Expected identifier after LET");
    }
    // トークンのテキストは null 終端済みで評価中は不変なので、コピーせず参照する
    const char* var_name = tokens.tokens[pos].text;
    pos++;
    
    int arr_idx, arr_idx2;
    parse_optional_indices(tokens, pos, arr_idx, arr_idx2);
    
    require_token(tokens, pos, TokenType::ASSIGN, "Syntax Error: Expected assignment");
    pos++;
    Value result = parse_relation(tokens, pos);
    
    int nlen = strlen(var_name);
    bool is_str_var = (nlen > 0 && var_name[nlen-1] == '$');
    bool is_int_var = (nlen > 0 && var_name[nlen-1] == '%');
    
    if (is_str_var && result.type != Value::Type::STR) throw std::runtime_error("Type Mismatch: Assigning NUM to STR variable");
    if (!is_str_var && result.type == Value::Type::STR) throw std::runtime_error("Type Mismatch: Assigning STR to NUM variable");

    if (is_int_var) {
        result = Value((int)result.num_val);
    } else if (!is_str_var && result.type == Value::Type::INT) {
        result = Value(result.num_val);
    }

    set_variable_at(var_name, arr_idx, arr_idx2, result);
}

void execute_input(const TokenList& tokens, int& pos) {
    pos++;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::HASH) {
        execute_input_file(tokens, pos); // INPUT #n, ...
        return;
    }
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::STRING) {
        basic_print(tokens.tokens[pos].text);
        pos++;
        if (pos < tokens.size && (tokens.tokens[pos].type == TokenType::COMMA || tokens.tokens[pos].type == TokenType::SEMICOLON)) pos++;
    } else {
        basic_print("? ");
    }
    
    while (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE) {
        require_token(tokens, pos, TokenType::IDENTIFIER, "Syntax Error: INPUT expects identifier");
        char var_name[64];
        copy_string(var_name, sizeof(var_name), tokens.tokens[pos].text);
        pos++;
        
        int arr_idx, arr_idx2;
        parse_optional_indices(tokens, pos, arr_idx, arr_idx2);
        
        char in_buf[128] = "";
        // 入力待ちの間に電源ボタンが長押しされたら、電源を切りに行ったあと
        // ここへ戻る。プログラムは既に停止済みなので変数へは書かずに抜ける
        if (!line_input_read_line(in_buf, sizeof(in_buf))) return;
        
        int nlen = strlen(var_name);
        bool is_str_var = (nlen > 0 && var_name[nlen-1] == '$');
        bool is_int_var = (nlen > 0 && var_name[nlen-1] == '%');
        Value val;
        if (is_str_var) {
            val = Value(in_buf);
        } else if (is_int_var) {
            val = Value((int)atof(in_buf));
        } else {
            val = Value((float)atof(in_buf));
        }
        
        set_variable_at(var_name, arr_idx, arr_idx2, val);
        
        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
        else break;
    }
}

// PRINT USING "書式"; 値 [; 値 ...]
//   # = 数値の桁 / . = 小数点 / & = 文字列全体 / ! = 文字列の先頭 1 文字。
//   その他の文字はそのまま出力。値が余れば書式を繰り返す。桁あふれは % 付き
static void execute_print_using(const TokenList& tokens, int& pos) {
    pos++; // USING
    Value fmt = parse_relation(tokens, pos);
    if (fmt.type != Value::Type::STR) throw std::runtime_error("Type Mismatch: USING expects format string");
    require_token(tokens, pos, TokenType::SEMICOLON, "Syntax Error: Expected ';' after USING format"); pos++;

    Value vals[16];
    int nvals = 0;
    bool newline = true;
    while (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE &&
           tokens.tokens[pos].type != TokenType::COLON &&
           tokens.tokens[pos].type != TokenType::REM) {
        if (tokens.tokens[pos].type == TokenType::COMMA ||
            tokens.tokens[pos].type == TokenType::SEMICOLON) {
            pos++;
            newline = false;
            continue;
        }
        if (nvals < 16) vals[nvals++] = parse_relation(tokens, pos);
        else parse_relation(tokens, pos);
        newline = true;
    }

    char out[256] = "";
    const char* f = fmt.str_val;
    int flen = (int)strlen(f);
    int fi = 0, vi = 0, consumed_in_pass = 0;
    while (vi < nvals) {
        if (fi >= flen) {
            if (consumed_in_pass == 0) break; // 値を消費しない書式 → 打ち切り
            fi = 0;
            consumed_in_pass = 0;
        }
        char c = f[fi];
        if (c == '#') {
            int ip = 0, dp = -1;
            while (fi < flen && f[fi] == '#') { ip++; fi++; }
            if (fi < flen && f[fi] == '.') {
                fi++; dp = 0;
                while (fi < flen && f[fi] == '#') { dp++; fi++; }
            }
            Value& v = vals[vi++]; consumed_in_pass++;
            if (!v.is_numeric()) throw std::runtime_error("Type Mismatch in PRINT USING");
            int width = ip + (dp >= 0 ? dp + 1 : 0);
            char nbuf[48];
            snprintf(nbuf, sizeof(nbuf), "%*.*f", width, (dp >= 0 ? dp : 0), v.num_val);
            if ((int)strlen(nbuf) > width)
                strncat(out, "%", sizeof(out) - strlen(out) - 1); // 桁あふれの印
            strncat(out, nbuf, sizeof(out) - strlen(out) - 1);
        } else if (c == '&') {
            fi++;
            Value& v = vals[vi++]; consumed_in_pass++;
            strncat(out, v.c_str(), sizeof(out) - strlen(out) - 1);
        } else if (c == '!') {
            fi++;
            Value& v = vals[vi++]; consumed_in_pass++;
            char one[2] = { (v.type == Value::Type::STR && v.str_val[0]) ? v.str_val[0] : ' ', '\0' };
            strncat(out, one, sizeof(out) - strlen(out) - 1);
        } else {
            char lit[2] = { c, '\0' };
            strncat(out, lit, sizeof(out) - strlen(out) - 1);
            fi++;
        }
    }
    // 値を使い切ったら、次のフィールドの手前までのリテラルを出し切る（"[&]" の閉じ括弧など）
    while (fi < flen && f[fi] != '#' && f[fi] != '&' && f[fi] != '!') {
        char lit[2] = { f[fi], '\0' };
        strncat(out, lit, sizeof(out) - strlen(out) - 1);
        fi++;
    }
    if (newline) strncat(out, "\n", sizeof(out) - strlen(out) - 1);
    basic_print(out);
}

// GET 変数：キーが押されていれば取得、なければ空（文字変数）/ 0（数値変数）。
// 待たずに戻るので、ゲームのリアルタイム入力に使える。
void execute_get(const TokenList& tokens, int& pos) {
    pos++;
    require_token(tokens, pos, TokenType::IDENTIFIER, "Syntax Error: GET expects a variable");
    char var_name[64];
    copy_string(var_name, sizeof(var_name), tokens.tokens[pos].text);
    pos++;

    int key = hal_system_get_key();

    int nlen = strlen(var_name);
    bool is_str_var = (nlen > 0 && var_name[nlen - 1] == '$');

    if (is_str_var) {
        char s[2] = { (key > 0) ? (char)key : '\0', '\0' };
        set_variable(var_name, Value(s));
    } else {
        // 数値変数には文字コードを入れる（未入力は 0）
        set_variable(var_name, Value(key));
    }
}

void execute_mid_statement(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::LPAREN, "Expected '('"); pos++;
    
    char var_name[64];
    require_token(tokens, pos, TokenType::IDENTIFIER, "Expected string variable name");
    copy_string(var_name, sizeof(var_name), tokens.tokens[pos].text);
    pos++;
    
    require_token(tokens, pos, TokenType::COMMA, "Expected ','"); pos++;
    Value v_start = parse_relation(tokens, pos);
    int start = static_cast<int>(v_start.num_val) - 1;
    
    int len = -1;
    if (tokens.tokens[pos].type == TokenType::COMMA) {
        pos++;
        Value v_len = parse_relation(tokens, pos);
        len = static_cast<int>(v_len.num_val);
    }
    
    require_token(tokens, pos, TokenType::RPAREN, "Expected ')'"); pos++;
    require_token(tokens, pos, TokenType::ASSIGN, "Expected '='"); pos++;
    
    Value replacement = parse_relation(tokens, pos);
    if (replacement.type != Value::Type::STR) throw std::runtime_error("Type Mismatch: Expected string replacement");
    
    Value original;
    if (!get_variable(var_name, original) || original.type != Value::Type::STR)
        throw std::runtime_error("Variable Error: String variable not found");
    
    int orig_len = strlen(original.str_val);
    if (start < 0) start = 0;
    if (start >= orig_len) return; 
    
    int replace_len = strlen(replacement.str_val);
    if (len != -1 && len < replace_len) replace_len = len;
    if (start + replace_len > orig_len) replace_len = orig_len - start;
    
    memcpy(original.str_val + start, replacement.str_val, replace_len);
    set_variable(var_name, original); 
}

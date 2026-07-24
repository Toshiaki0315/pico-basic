#include "parser_internal.h"
#include "hal_display.h"
#include "hal_sdcard.h" // Needed for files if not decoupled, wait, IO is in the other file.
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <cstdio>

void basic_print(const char* s) {
    hal_display_print(s);
    printf("%s", s);
}

void execute_print(const TokenList& tokens, int& pos) {
    pos++;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::HASH) {
        execute_print_file(tokens, pos); // PRINT #n, ...
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
    strncpy(fname, tokens.tokens[pos].text, MAX_TOKEN_LEN - 1);
    fname[MAX_TOKEN_LEN - 1] = '\0';
    if (!(fname[0] == 'F' && fname[1] == 'N' && fname[2] != '\0'))
        throw std::runtime_error("Syntax Error: DEF name must start with FN");
    pos++;

    require_token(tokens, pos, TokenType::LPAREN, "Syntax Error: Expected '(' after FN name"); pos++;
    require_token(tokens, pos, TokenType::IDENTIFIER, "Syntax Error: Expected parameter name");
    char pname[MAX_TOKEN_LEN];
    strncpy(pname, tokens.tokens[pos].text, MAX_TOKEN_LEN - 1);
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
    strncpy(f->name, fname, MAX_TOKEN_LEN - 1);  f->name[MAX_TOKEN_LEN - 1] = '\0';
    strncpy(f->param, pname, MAX_TOKEN_LEN - 1); f->param[MAX_TOKEN_LEN - 1] = '\0';
    strncpy(f->body, body, sizeof(f->body) - 1); f->body[sizeof(f->body) - 1] = '\0';
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
        strncpy(var_name, tokens.tokens[pos].text, sizeof(var_name)-1);
        var_name[sizeof(var_name)-1] = '\0';
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
        
        if (arr_idx >= 0) {
            ArrayRef* arr = get_array(var_name);
            if (!arr) throw std::runtime_error("Array not dimensioned");
            int flat_idx = flatten_array_index(arr, arr_idx, arr_idx2);
            write_heap_value(arr->start_addr + (flat_idx * 8), val);
        } else {
            set_variable(var_name, val);
        }
        
        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) { pos++; } else { break; }
    }
}

// 分岐先の行番号を得る。`*LABEL` ならラベル表から、そうでなければ数式として評価する。
static int parse_branch_target(const TokenList& tokens, int& pos) {
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::LABEL) {
        int line = resolve_label(tokens.tokens[pos].text);
        if (line < 0) throw std::runtime_error("Undefined label");
        pos++;
        return line;
    }
    Value v = parse_relation(tokens, pos);
    if (!v.is_numeric())
        throw std::runtime_error("Type Mismatch: branch requires number");
    return static_cast<int>(v.num_val);
}

void execute_goto(const TokenList& tokens, int& pos) {
    pos++;
    current_line = parse_branch_target(tokens, pos);
    if (find_program_line(current_line) == 0xFFFF) throw std::runtime_error("GOTO target line not found");
    branch_taken = true;
}

// GOSUB のフレームを積んで target へ分岐する。
// 復帰先は「現在行の resume_pos の位置」（呼び出し文の直後の `:` か行末）。
static void gosub_branch(int target, int resume_pos, const char* overflow_msg) {
    if (call_stack_ptr >= MAX_CALL_STACK) throw std::runtime_error(overflow_msg);
    call_stack[call_stack_ptr] = current_line;
    call_stack_pos[call_stack_ptr] = resume_pos;
    call_stack_ptr++;
    current_line = target;
    branch_taken = true;
}

void execute_gosub(const TokenList& tokens, int& pos) {
    pos++;
    int target = parse_branch_target(tokens, pos);
    if (find_program_line(target) == 0xFFFF) throw std::runtime_error("GOSUB target line not found");
    gosub_branch(target, pos, "Out of Memory: Call Stack Limit Reached");
}

void execute_return(const TokenList& tokens, int& pos) {
    pos++;
    if (call_stack_ptr == 0) throw std::runtime_error("RETURN WITHOUT GOSUB");
    call_stack_ptr--;
    int returned_from = call_stack[call_stack_ptr];
    int resume        = call_stack_pos[call_stack_ptr];
    if (find_program_line(returned_from) == 0xFFFF)
        throw std::runtime_error("Original line disappeared during GOSUB");
    // GOSUB の直後（同じ行の続き）から再開する。GOSUB が行末なら resume は行末を指し、
    // 実行ループはその行で何もせず次の行へ進む（＝従来どおり次の行に戻るのと同じ）
    current_line = returned_from;
    branch_resume_pos = resume;
    branch_taken = true;
}

// THEN / ELSE の直後を実行する。`*LABEL` や行番号だけなら GOTO 扱いにする
// （Hu-BASIC の `IF ... THEN *LOOP` 記法）。それ以外は通常の文として実行する。
static void execute_then_branch(const TokenList& tokens, int& pos) {
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::LABEL) {
        int line = resolve_label(tokens.tokens[pos].text);
        if (line < 0) throw std::runtime_error("Undefined label");
        current_line = line;
        branch_taken = true;
        pos = tokens.size;
        return;
    }
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::NUMBER) {
        // `THEN 100` は `THEN GOTO 100`
        current_line = atoi(tokens.tokens[pos].text);
        if (find_program_line(current_line) == 0xFFFF) throw std::runtime_error("GOTO target line not found");
        branch_taken = true;
        pos = tokens.size;
        return;
    }
    execute_statement(tokens, pos);
}

void execute_if(const TokenList& tokens, int& pos) {
    pos++; // IF を飛ばす（以降 ELSEIF ごとにこのループを回す）
    while (true) {
        Value condition_result = parse_relation(tokens, pos);
        if (!condition_result.is_numeric())
            throw std::runtime_error("Type Mismatch: IF condition must be numeric");

        // 通常は THEN が必要だが、`IF 条件 GOTO 行` / `IF 条件 GOSUB 行` のように
        // THEN を省いて分岐命令を直接続ける書き方（S-BASIC 等）も認める。
        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::THEN) {
            pos++;
        } else if (pos < tokens.size && (tokens.tokens[pos].type == TokenType::GOTO ||
                                         tokens.tokens[pos].type == TokenType::GOSUB)) {
            // THEN 省略。GOTO/GOSUB をそのまま THEN 節として実行する
        } else {
            throw std::runtime_error("Syntax Error: Missing THEN in IF statement");
        }

        if (condition_result.num_val != 0.0f) {
            // 条件成立: THEN 節を実行し、後続の ELSE / ELSEIF 節は読み飛ばす
            // （`:` で続く複数文は run ループが処理し、ELSE/ELSEIF で止まる）
            execute_then_branch(tokens, pos);
            if (pos < tokens.size && (tokens.tokens[pos].type == TokenType::ELSE ||
                                      tokens.tokens[pos].type == TokenType::ELSEIF)) {
                pos = tokens.size;
            }
            return;
        }

        // 条件不成立: 同じ行の ELSE / ELSEIF まで読み飛ばす
        while (pos < tokens.size && tokens.tokens[pos].type != TokenType::ELSE &&
               tokens.tokens[pos].type != TokenType::ELSEIF &&
               tokens.tokens[pos].type != TokenType::END_OF_FILE) {
            pos++;
        }
        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::ELSEIF) {
            pos++;       // ELSEIF を新たな IF 条件として続行
            continue;
        }
        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::ELSE) {
            pos++;
            execute_then_branch(tokens, pos);
            return;
        }
        pos = tokens.size; // ELSE も ELSEIF も無い
        return;
    }
}

void execute_for(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::IDENTIFIER, "Syntax Error: Expected Identifier in FOR");
    char var_name[64];
    strncpy(var_name, tokens.tokens[pos].text, sizeof(var_name)-1);
    var_name[sizeof(var_name)-1] = '\0';
    pos++;

    require_token(tokens, pos, TokenType::ASSIGN, "Syntax Error: Expected '=' in FOR");
    pos++;
    Value start_val = parse_relation(tokens, pos);
    if (!start_val.is_numeric()) throw std::runtime_error("Type Mismatch: FOR start value");
    
    require_token(tokens, pos, TokenType::TO, "Syntax Error: Expected TO in FOR");
    pos++;
    Value end_val = parse_relation(tokens, pos);
    if (!end_val.is_numeric()) throw std::runtime_error("Type Mismatch: FOR end value");
    
    float step_val = 1.0f;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::STEP) {
        pos++;
        Value step_v = parse_relation(tokens, pos);
        if (!step_v.is_numeric()) throw std::runtime_error("Type Mismatch: FOR step value");
        step_val = step_v.num_val;
    }
    
    set_variable(var_name, start_val);
    if (for_stack_ptr >= MAX_FOR_STACK) throw std::runtime_error("Out of Memory: FOR Stack Limit Reached");
    ForLoopContext ctx = {};
    strncpy(ctx.var_name, var_name, sizeof(ctx.var_name)-1);
    ctx.target = end_val.num_val;
    ctx.step = step_val;
    ctx.loop_start_line = current_line;
    ctx.loop_start_pos = pos;
    for_stack[for_stack_ptr++] = ctx;
}

void execute_next(const TokenList& tokens, int& pos) {
    pos++; 
    if (for_stack_ptr == 0) throw std::runtime_error("NEXT without FOR");
    
    char var_name[64] = "";
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::IDENTIFIER) {
        strncpy(var_name, tokens.tokens[pos].text, sizeof(var_name)-1);
        var_name[sizeof(var_name)-1] = '\0';
        pos++;
    }
    
    ForLoopContext& ctx = for_stack[for_stack_ptr - 1];
    if (strlen(var_name) > 0 && strcmp(ctx.var_name, var_name) != 0) throw std::runtime_error("NEXT variable does not match FOR");
    
    Value v_val;
    if (get_variable(ctx.var_name, v_val)) {
        v_val.num_val += ctx.step;
        if (v_val.type == Value::Type::INT) {
            v_val.int_val = (int)v_val.num_val;
        }
        set_variable(ctx.var_name, v_val);
    } else {
        throw std::runtime_error("Intern Error: FOR variable missing");
    }
    
    bool cont = (ctx.step > 0) ? (v_val.num_val <= ctx.target) : (v_val.num_val >= ctx.target);
    if (cont) {
        // ループ本体の先頭（FOR の直後）へ戻る。FOR と同じ行に本体があってもよい
        branch_taken = true;
        current_line = ctx.loop_start_line;
        branch_resume_pos = ctx.loop_start_pos;
    } else {
        for_stack_ptr--;
    }
}

void execute_dim(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::IDENTIFIER, "Syntax Error: Expected identifier after DIM");
    char var_name[64];
    strncpy(var_name, tokens.tokens[pos].text, sizeof(var_name)-1);
    var_name[sizeof(var_name)-1] = '\0';
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
    strncpy((char*)&logical_memory[table_addr], var_name, 8);
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
    char var_name[64];
    if (explicit_let) {
        pos++;
        require_token(tokens, pos, TokenType::IDENTIFIER, "Syntax Error: Expected identifier after LET");
    }
    strncpy(var_name, tokens.tokens[pos].text, sizeof(var_name)-1);
    var_name[sizeof(var_name)-1] = '\0';
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

    if (arr_idx >= 0) {
        ArrayRef* arr = get_array(var_name);
        if (!arr) throw std::runtime_error("Array not dimensioned");
        int flat_idx = flatten_array_index(arr, arr_idx, arr_idx2);
        write_heap_value(arr->start_addr + (flat_idx * 8), result);
    } else {
        set_variable(var_name, result);
    }
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
        strncpy(var_name, tokens.tokens[pos].text, sizeof(var_name)-1);
        var_name[sizeof(var_name)-1] = '\0';
        pos++;
        
        int arr_idx, arr_idx2;
        parse_optional_indices(tokens, pos, arr_idx, arr_idx2);
        
        char in_buf[128] = "";
        hal_display_input(in_buf, sizeof(in_buf));
        
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
        
        if (arr_idx >= 0) {
            ArrayRef* arr = get_array(var_name);
            if (!arr) throw std::runtime_error("Array not dimensioned");
            int flat_idx = flatten_array_index(arr, arr_idx, arr_idx2);
            write_heap_value(arr->start_addr + (flat_idx * 8), val);
        } else {
            set_variable(var_name, val);
        }
        
        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
        else break;
    }
}

void execute_end(const TokenList& tokens, int& pos) {
    basic_files_close_all(); // 書きかけのファイルを確定させる

    pos = tokens.size;
    current_line = -1;
    branch_taken = true;
}

void execute_stop(const TokenList& tokens, int& pos) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Break in %d\n", current_line);
    basic_print(buf);
    pos = tokens.size;
    current_line = -1;
    branch_taken = true;
}

// REPEAT … UNTIL 条件（後判定ループ）。
// UNTIL の条件が偽の間、REPEAT の次の行へ戻る。
// FOR/NEXT と同じく行番号で戻るため、REPEAT は行頭に置く前提。
void execute_repeat(const TokenList& tokens, int& pos) {
    pos++;

    // 同じ REPEAT に再突入したとき（UNTIL から戻ってきた場合）は
    // 二重に積まない。行番号で判定する
    if (repeat_stack_ptr > 0 && repeat_stack_line[repeat_stack_ptr - 1] == current_line) {
        return;
    }
    if (repeat_stack_ptr >= MAX_REPEAT_STACK)
        throw std::runtime_error("Out of Memory: REPEAT Stack Limit Reached");

    repeat_stack_line[repeat_stack_ptr] = current_line;
    repeat_stack_pos[repeat_stack_ptr] = pos; // REPEAT の直後（ループ本体の先頭）
    repeat_stack_ptr++;
}

void execute_until(const TokenList& tokens, int& pos) {
    pos++;
    if (repeat_stack_ptr == 0) throw std::runtime_error("UNTIL without REPEAT");

    Value cond = parse_relation(tokens, pos);
    if (!cond.is_numeric())
        throw std::runtime_error("Type Mismatch: UNTIL condition must be numeric");

    if (cond.num_val != 0.0f) {
        // 条件成立 → ループ終了
        repeat_stack_ptr--;
    } else {
        // 条件不成立 → REPEAT の直後（ループ本体の先頭）へ戻る
        current_line = repeat_stack_line[repeat_stack_ptr - 1];
        branch_resume_pos = repeat_stack_pos[repeat_stack_ptr - 1];
        branch_taken = true;
    }
}

// GET 変数：キーが押されていれば取得、なければ空（文字変数）/ 0（数値変数）。
// 待たずに戻るので、ゲームのリアルタイム入力に使える。
void execute_get(const TokenList& tokens, int& pos) {
    pos++;
    require_token(tokens, pos, TokenType::IDENTIFIER, "Syntax Error: GET expects a variable");
    char var_name[64];
    strncpy(var_name, tokens.tokens[pos].text, sizeof(var_name) - 1);
    var_name[sizeof(var_name) - 1] = '\0';
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

void execute_on(const TokenList& tokens, int& pos) {
    pos++; 
    Value idx_val = parse_relation(tokens, pos);
    int idx = static_cast<int>(idx_val.num_val);
    
    if (pos >= tokens.size) throw std::runtime_error("Syntax Error: Expected GOTO/GOSUB after ON");
    TokenType type = tokens.tokens[pos].type;
    if (type != TokenType::GOTO && type != TokenType::GOSUB)
        throw std::runtime_error("Syntax Error: Expected GOTO or GOSUB");
    pos++;
    
    // GOSUB からの復帰を行内の続きに戻せるよう、一致後もリストを最後まで読み進めて
    // pos を ON 文全体の後ろ（`:` か行末）に置く
    int current_idx = 1;
    int target = -1;
    bool found = false;
    while (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE &&
           tokens.tokens[pos].type != TokenType::COLON) {
        int t;
        if (tokens.tokens[pos].type == TokenType::LABEL) {
            t = resolve_label(tokens.tokens[pos].text);
            if (t < 0) throw std::runtime_error("Undefined label");
            pos++;
        } else {
            require_token(tokens, pos, TokenType::NUMBER, "Expected line number");
            t = atoi(tokens.tokens[pos].text);
            pos++;
        }
        if (current_idx == idx) { target = t; found = true; }

        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
            pos++;
            current_idx++;
        } else break;
    }

    if (found) {
        if (find_program_line(target) == 0xFFFF) throw std::runtime_error("GOTO target line not found");
        if (type == TokenType::GOSUB) {
            gosub_branch(target, pos, "GOSUB Stack Overflow"); // ON 文全体の後ろへ復帰する
        } else {
            current_line = target;
            branch_taken = true;
        }
    }
    // idx が範囲外なら何もせず次の文／行へ進む
}

void execute_mid_statement(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::LPAREN, "Expected '('"); pos++;
    
    char var_name[64];
    require_token(tokens, pos, TokenType::IDENTIFIER, "Expected string variable name");
    strncpy(var_name, tokens.tokens[pos].text, sizeof(var_name)-1);
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

void execute_not_implemented(const TokenList& tokens, int& pos) {
    char buf[128];
    snprintf(buf, sizeof(buf), "Notice: Command '%s' is registered but not yet implemented.\n", tokens.tokens[pos].text);
    basic_print(buf);
    pos = tokens.size;
}

// 互換のための空実行。命令と（あれば）引数を次の `:` まで読み飛ばし、何もしない。
// INIT / NEWON は元の X1 Hu-BASIC ではメモリ領域予約の命令だが、この実装には
// 対応する概念がないため受理して無視する（古いプログラムがエラーで止まらないように）。
void execute_noop_statement(const TokenList& tokens, int& pos) {
    pos++; // 命令トークン
    while (pos < tokens.size && tokens.tokens[pos].type != TokenType::COLON &&
           tokens.tokens[pos].type != TokenType::END_OF_FILE) {
        pos++;
    }
}

void execute_statement(const TokenList& tokens, int& pos) {
    if (pos >= tokens.size || tokens.tokens[pos].type == TokenType::END_OF_FILE) return;

    switch (tokens.tokens[pos].type) {
        case TokenType::PRINT:   execute_print(tokens, pos); break;
        case TokenType::DATA:    pos = tokens.size; break; 
        case TokenType::RESTORE: pos++; data_ptr = 0; break;
        case TokenType::READ:    execute_read(tokens, pos); break;
        case TokenType::GOTO:    execute_goto(tokens, pos); break;
        case TokenType::GOSUB:   execute_gosub(tokens, pos); break;
        case TokenType::RETURN:  execute_return(tokens, pos); break;
        case TokenType::IF:      execute_if(tokens, pos); break;
        case TokenType::FILES:   execute_files(tokens, pos); break;
        case TokenType::KILL:    execute_kill(tokens, pos); break;
        case TokenType::NAME:    execute_name(tokens, pos); break;
        case TokenType::ON:      execute_on(tokens, pos); break;
        case TokenType::FOR:     execute_for(tokens, pos); break;
        case TokenType::NEXT:    execute_next(tokens, pos); break;
        case TokenType::DIM:     execute_dim(tokens, pos); break;
        case TokenType::INPUT:   execute_input(tokens, pos); break;
        case TokenType::END:     execute_end(tokens, pos); break;
        case TokenType::STOP:    execute_stop(tokens, pos); break;
        case TokenType::LET:     execute_assignment(tokens, pos, true); break;
        case TokenType::IDENTIFIER: 
            if (strcmp(tokens.tokens[pos].text, "MID$") == 0) execute_mid_statement(tokens, pos);
            else execute_assignment(tokens, pos, false); 
            break;
        
        case TokenType::COLOR:   execute_color(tokens, pos); break;
        case TokenType::PSET:    execute_pset(tokens, pos); break;
        case TokenType::LINE:    execute_line(tokens, pos); break;
        case TokenType::CIRCLE:  execute_circle(tokens, pos); break;
        case TokenType::CLS:     pos++; hal_display_cls(); break;
        case TokenType::LOCATE:  execute_locate(tokens, pos); break;
        case TokenType::WAIT:    execute_wait(tokens, pos); break;
        case TokenType::CLEAR:   pos++; execute_clear(); break;
        
        case TokenType::GPIO:    execute_gpio(tokens, pos); break;
        case TokenType::BRIGHTNESS: execute_brightness(tokens, pos); break;
        case TokenType::PAINT:   execute_paint(tokens, pos); break;
        case TokenType::GET_AT:  execute_get_at(tokens, pos); break;
        case TokenType::PUT_AT:  execute_put_at(tokens, pos); break;
        
        case TokenType::SAVE:    execute_save(tokens, pos); break;
        case TokenType::OPEN:    execute_open(tokens, pos); break;
        case TokenType::CLOSE:   execute_close(tokens, pos); break;
        case TokenType::LOAD:    execute_load(tokens, pos); break;

        case TokenType::INIT: case TokenType::NEWON:
                                 execute_noop_statement(tokens, pos); break;
        case TokenType::LABEL:   pos++; break; // 行頭のラベル定義は実行時は素通り
        case TokenType::REM:     pos = tokens.size; break; // コメント。行末まで読み飛ばす
        case TokenType::DEF:     execute_def(tokens, pos); break;
        case TokenType::POKE:    execute_poke(tokens, pos); break;
        case TokenType::AUTO:    execute_noop_statement(tokens, pos); break; // AUTO は repl が処理。プログラム内では無視
        case TokenType::WIDTH:   execute_width(tokens, pos); break;
        case TokenType::CONSOLE: execute_console(tokens, pos); break;
        case TokenType::REPEAT:  execute_repeat(tokens, pos); break;
        case TokenType::UNTIL:   execute_until(tokens, pos); break;
        case TokenType::GET:     execute_get(tokens, pos); break;
        case TokenType::WINDOW:  execute_window(tokens, pos); break;
        case TokenType::POLY:    execute_poly(tokens, pos); break;
        case TokenType::BEEP:    execute_beep(tokens, pos); break;
        case TokenType::PLAY:    execute_music(tokens, pos); break; 
        case TokenType::MUSIC:   execute_music(tokens, pos); break;
        case TokenType::SOUND:   execute_sound(tokens, pos); break;

        default: throw std::runtime_error("Syntax Error: Unrecognized statement");
    }
}

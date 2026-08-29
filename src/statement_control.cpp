#include "parser_internal.h"
#include "parser.h"
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// 制御構造の実行。
// GOTO / GOSUB / RETURN / IF…THEN / FOR…NEXT / WHILE…WEND / REPEAT…UNTIL /
// ON…GOTO / RESUME / END / STOP と、それらが共有する分岐先の解決。
//
// 分岐に使うスタック（call_stack / for_stack / while_stack / repeat_stack）は
// parser_internal.h で宣言してあり、実体は parser.cpp が持つ。

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

void execute_return(const TokenList&, int& pos) {
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

void execute_end(const TokenList& tokens, int& pos) {
    basic_files_close_all(); // 書きかけのファイルを確定させる

    pos = tokens.size;
    current_line = -1;
    branch_taken = true;
}

void execute_stop(const TokenList& tokens, int& pos) {
    pos++;
    // CONT で STOP の直後（同じ行の続き）から再開できるようにする
    cont_line = current_line;
    cont_pos = pos;
    cont_valid = true;
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
// 対応する WEND を前方に探して、その直後へ分岐する（WHILE 不成立時）
static void skip_to_matching_wend(const TokenList& tokens, int pos_after_cond) {
    int depth = 1;
    for (int k = pos_after_cond; k < tokens.size; k++) {
        if (tokens.tokens[k].type == TokenType::WHILE) depth++;
        else if (tokens.tokens[k].type == TokenType::WEND) {
            depth--;
            if (depth == 0) {
                branch_taken = true;          // 同じ行の WEND の直後から再開
                branch_resume_pos = k + 1;
                return;
            }
        }
    }
    int line = current_line;
    while (true) {
        uint16_t idx = get_next_program_line(line);
        if (idx == 0xFFFF) throw std::runtime_error("WHILE without WEND");
        line = prog_line_no(idx);
        static TokenList t; // TokenList は大きいのでスタックに積まない
        t = get_detokenized_line(find_program_line(line));
        for (int k = 0; k < t.size; k++) {
            if (t.tokens[k].type == TokenType::WHILE) depth++;
            else if (t.tokens[k].type == TokenType::WEND) {
                depth--;
                if (depth == 0) {
                    current_line = line;
                    branch_taken = true;
                    branch_resume_pos = k + 1;
                    return;
                }
            }
        }
    }
}

// WHILE 条件 ... WEND — 前判定ループ。WEND が WHILE へ戻り、条件を毎回評価する
void execute_while(const TokenList& tokens, int& pos) {
    int while_pos = pos; // WHILE トークン自身の位置（WEND の戻り先）
    pos++;
    Value cond = parse_relation(tokens, pos);
    if (!cond.is_numeric()) throw std::runtime_error("Type Mismatch: WHILE condition must be numeric");

    bool on_top = (while_stack_ptr > 0 &&
                   while_stack_line[while_stack_ptr - 1] == current_line &&
                   while_stack_pos[while_stack_ptr - 1] == while_pos);
    if (cond.num_val != 0.0f) {
        if (!on_top) {
            if (while_stack_ptr >= MAX_WHILE_STACK)
                throw std::runtime_error("Out of Memory: WHILE Stack Limit Reached");
            while_stack_line[while_stack_ptr] = current_line;
            while_stack_pos[while_stack_ptr] = while_pos;
            while_stack_ptr++;
        }
        return; // 本体へ
    }
    if (on_top) while_stack_ptr--; // ループ終了
    skip_to_matching_wend(tokens, pos);
}

void execute_wend(const TokenList&, int& pos) {
    pos++;
    if (while_stack_ptr == 0) throw std::runtime_error("WEND without WHILE");
    current_line = while_stack_line[while_stack_ptr - 1];
    branch_resume_pos = while_stack_pos[while_stack_ptr - 1]; // WHILE を再評価する
    branch_taken = true;
}

// RESUME [NEXT | 行番号 | *ラベル] — エラーハンドラから復帰する
void execute_resume(const TokenList& tokens, int& pos) {
    pos++;
    if (!in_error_handler) throw std::runtime_error("RESUME without error");
    in_error_handler = false;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::NEXT) {
        pos++;
        uint16_t idx = get_next_program_line(err_line); // エラー行の次から
        if (idx == 0xFFFF) { current_line = -1; branch_taken = true; return; }
        current_line = prog_line_no(idx);
    } else if (pos < tokens.size && (tokens.tokens[pos].type == TokenType::NUMBER ||
                                     tokens.tokens[pos].type == TokenType::LABEL)) {
        current_line = parse_branch_target(tokens, pos);
        if (find_program_line(current_line) == 0xFFFF)
            throw std::runtime_error("Undefined line number in RESUME");
    } else {
        current_line = err_line; // エラーを起こした行をやり直す
    }
    branch_taken = true;
}

void execute_repeat(const TokenList&, int& pos) {
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

void execute_on(const TokenList& tokens, int& pos) {
    pos++;
    // ON ERROR GOTO 行番号（0 で解除）
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::IDENTIFIER &&
        strcmp(tokens.tokens[pos].text, "ERROR") == 0) {
        pos++;
        require_token(tokens, pos, TokenType::GOTO, "Syntax Error: Expected GOTO after ON ERROR"); pos++;
        int target;
        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::NUMBER) {
            target = atoi(tokens.tokens[pos].text); pos++;
        } else if (pos < tokens.size && tokens.tokens[pos].type == TokenType::LABEL) {
            target = resolve_label(tokens.tokens[pos].text);
            if (target < 0) throw std::runtime_error("Undefined label");
            pos++;
        } else {
            throw std::runtime_error("Syntax Error: Expected line number after ON ERROR GOTO");
        }
        if (target != 0 && find_program_line(target) == 0xFFFF)
            throw std::runtime_error("Undefined line number");
        error_handler_line = target;
        return;
    }
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

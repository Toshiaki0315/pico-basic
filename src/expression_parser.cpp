#include "parser_internal.h"
#include "hal_touch.h"
#include "hal_display.h"
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <cmath>

static int to_int_operand(const Value& v, const char* op);

void require_token(const TokenList& tokens, int& pos, TokenType expected, const char* err_msg) {
    if (pos >= tokens.size || tokens.tokens[pos].type != expected) {
        throw std::runtime_error(err_msg);
    }
}

static bool is_builtin_function(const char* name) {
    static const char* const NAMES[] = {
        "ABS", "INT", "RND", "SGN", "SQR", "SIN", "COS", "TAN", "LOG", "EXP",
        "LEN", "MID$", "LEFT$", "RIGHT$", "CHR$", "ASC", "VAL", "STR$",
        "PEEK", "TOUCH",
        "INSTR", "STRING$", "SPACE$", "HEX$", "POINT", "EOF",
    };
    for (const char* n : NAMES) {
        if (strcmp(name, n) == 0) return true;
    }
    return false;
}

// 引数の数・型が合わないときの共通エラー（メッセージは従来と同一）
static void need_args(bool ok, const char* fn_name) {
    if (!ok) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Type Mismatch/Arg Count in %s", fn_name);
        throw std::runtime_error(buf);
    }
}

static Value evaluate_builtin_function(const char* var_name, Value* args, int arg_count) {
    if (strcmp(var_name, "ABS") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "ABS");
        return Value(std::abs(args[0].num_val));
    } else if (strcmp(var_name, "INT") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "INT");
        return Value(std::floor(args[0].num_val));
    } else if (strcmp(var_name, "SGN") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "SGN");
        return Value((args[0].num_val > 0) ? 1.0f : (args[0].num_val < 0) ? -1.0f : 0.0f);
    } else if (strcmp(var_name, "SQR") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "SQR");
        if (args[0].num_val < 0) throw std::runtime_error("Math Error: SQR of negative number");
        return Value(std::sqrt(args[0].num_val));
    } else if (strcmp(var_name, "SIN") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "SIN");
        return Value(std::sin(args[0].num_val));
    } else if (strcmp(var_name, "COS") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "COS");
        return Value(std::cos(args[0].num_val));
    } else if (strcmp(var_name, "TAN") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "TAN");
        return Value(std::tan(args[0].num_val));
    } else if (strcmp(var_name, "LOG") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "LOG");
        if (args[0].num_val <= 0) throw std::runtime_error("Math Error: LOG of non-positive number");
        return Value(std::log(args[0].num_val));
    } else if (strcmp(var_name, "EXP") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "EXP");
        return Value(std::exp(args[0].num_val));
    } else if (strcmp(var_name, "PEEK") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "PEEK");
        int addr = static_cast<int>(args[0].num_val);
        if (addr < 0 || addr > 65535) throw std::runtime_error("Illegal function call: PEEK address out of range");
        return Value((int)logical_memory[addr]); // 論理メモリの 1 バイト（0-255）
    } else if (strcmp(var_name, "RND") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "RND");
        float n = args[0].num_val;
        if (n < 0) {
            srand(static_cast<unsigned int>(std::abs(n)));
            last_rnd_val = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
            return Value(last_rnd_val);
        } else if (n == 0) {
            return Value(last_rnd_val);
        } else {
            last_rnd_val = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
            return Value(last_rnd_val * n);
        }
    } else if (strcmp(var_name, "LEN") == 0) {
        need_args(arg_count == 1 && args[0].type == Value::Type::STR, "LEN");
        return Value((float)strlen(args[0].str_val));
    } else if (strcmp(var_name, "MID$") == 0) {
        need_args(arg_count == 3 && args[0].type == Value::Type::STR && args[1].is_numeric() && args[2].is_numeric(), "MID$");
        int start = static_cast<int>(args[1].num_val) - 1; 
        int len = static_cast<int>(args[2].num_val);
        char* s = args[0].str_val;
        if (start < 0) start = 0;
        if (len < 0) len = 0;
        if (start >= (int)strlen(s)) return Value("");
        char buf[128];
        snprintf(buf, sizeof(buf), "%.*s", len, s + start);
        return Value(buf);
    } else if (strcmp(var_name, "LEFT$") == 0) {
        need_args(arg_count == 2 && args[0].type == Value::Type::STR && args[1].is_numeric(), "LEFT$");
        int len = static_cast<int>(args[1].num_val);
        char* s = args[0].str_val;
        if (len < 0) len = 0;
        if (len >= (int)strlen(s)) return Value(s);
        char buf[128];
        snprintf(buf, sizeof(buf), "%.*s", len, s);
        return Value(buf);
    } else if (strcmp(var_name, "RIGHT$") == 0) {
        need_args(arg_count == 2 && args[0].type == Value::Type::STR && args[1].is_numeric(), "RIGHT$");
        int len = static_cast<int>(args[1].num_val);
        char* s = args[0].str_val;
        int s_len = strlen(s);
        if (len < 0) len = 0;
        if (len >= s_len) return Value(s);
        return Value(s + s_len - len);
    } else if (strcmp(var_name, "CHR$") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "CHR$");
        char buf[2] = {(char)args[0].num_val, '\0'};
        return Value(buf);
    } else if (strcmp(var_name, "ASC") == 0) {
        need_args(arg_count == 1 && args[0].type == Value::Type::STR, "ASC");
        if (args[0].str_val[0] == '\0') return Value(0.0f);
        return Value((float)static_cast<unsigned char>(args[0].str_val[0]));
    } else if (strcmp(var_name, "VAL") == 0) {
        need_args(arg_count == 1 && args[0].type == Value::Type::STR, "VAL");
        return Value((float)atof(args[0].str_val));
    } else if (strcmp(var_name, "STR$") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "STR$");
        char buf[32];
        snprintf(buf, sizeof(buf), "%g", args[0].num_val);
        return Value(buf);
    } else if (strcmp(var_name, "INSTR") == 0) {
        // INSTR(文字列, 検索語) または INSTR(開始位置, 文字列, 検索語)。1 始まり、無ければ 0
        int start = 1;
        const Value* hay;
        const Value* nee;
        if (arg_count == 2) {
            hay = &args[0]; nee = &args[1];
        } else {
            need_args(arg_count == 3 && args[0].is_numeric(), "INSTR");
            start = (int)args[0].num_val;
            hay = &args[1]; nee = &args[2];
        }
        need_args(hay->type == Value::Type::STR && nee->type == Value::Type::STR, "INSTR");
        int hlen = (int)strlen(hay->str_val);
        if (start < 1) start = 1;
        if (start > hlen) return Value(0);
        if (nee->str_val[0] == '\0') return Value(start); // 空文字列は開始位置に一致
        const char* found = strstr(hay->str_val + (start - 1), nee->str_val);
        return Value(found ? (int)(found - hay->str_val) + 1 : 0);
    } else if (strcmp(var_name, "STRING$") == 0) {
        // STRING$(個数, 文字列) / STRING$(個数, 文字コード) — 先頭 1 文字を繰り返す
        need_args(arg_count == 2 && args[0].is_numeric(), "STRING$");
        char ch;
        if (args[1].type == Value::Type::STR) {
            need_args(args[1].str_val[0] != '\0', "STRING$");
            ch = args[1].str_val[0];
        } else {
            ch = (char)(int)args[1].num_val;
        }
        int n = (int)args[0].num_val;
        if (n < 0) n = 0;
        if (n > 127) n = 127; // str_val の上限
        char buf[128];
        memset(buf, ch, n);
        buf[n] = '\0';
        return Value(buf);
    } else if (strcmp(var_name, "SPACE$") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "SPACE$");
        int n = (int)args[0].num_val;
        if (n < 0) n = 0;
        if (n > 127) n = 127;
        char buf[128];
        memset(buf, ' ', n);
        buf[n] = '\0';
        return Value(buf);
    } else if (strcmp(var_name, "HEX$") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "HEX$");
        char buf[16];
        snprintf(buf, sizeof(buf), "%X", (unsigned)(int)args[0].num_val);
        return Value(buf);
    } else if (strcmp(var_name, "POINT") == 0) {
        // POINT(x, y): 画面の色番号（0-15）を返す。画面外は -1。
        // 座標は WINDOW のユーザー座標系に従う（描画コマンドと同じ）
        need_args(arg_count == 2 && args[0].is_numeric() && args[1].is_numeric(), "POINT");
        int sx, sy;
        user_to_screen(args[0].num_val, args[1].num_val, sx, sy);
        if (sx < 0 || sx > 319 || sy < 0 || sy > 239) return Value(-1);
        uint16_t c565 = hal_graphics_get_pixel(sx, sy);
        for (int i = 0; i < 16; i++) {
            if (PALETTE[i] == c565) return Value(i);
        }
        return Value(-1); // パレット外の色（通常は起こらない）
    } else if (strcmp(var_name, "EOF") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "EOF");
        return Value(basic_file_eof((int)args[0].num_val));
    } else if (strcmp(var_name, "TOUCH") == 0) {
        need_args(arg_count == 1 && args[0].is_numeric(), "TOUCH");
        int n = static_cast<int>(args[0].num_val);
        switch (n) {
            case 0: return Value((float)hal_touch_get_x());
            case 1: return Value((float)hal_touch_get_y());
            case 2: return Value((float)hal_touch_is_touched());
            default: throw std::runtime_error("TOUCH argument must be 0, 1, or 2");
        }
    }
    throw std::runtime_error("Unknown Function");
}

static Value parse_power_expr(const TokenList& tokens, int& pos);

static Value parse_factor(const TokenList& tokens, int& pos) {
    if (pos >= tokens.size) return Value(0.0f);
    Token t = tokens.tokens[pos];
    
    if (t.type == TokenType::PLUS) {
        pos++; 
        Value v = parse_factor(tokens, pos);
        if (!v.is_numeric()) throw std::runtime_error("Type Mismatch: Unary + on string");
        return v;
    }
    if (t.type == TokenType::MINUS) {
        pos++; 
        Value v = parse_factor(tokens, pos);
        if (v.type == Value::Type::STR) throw std::runtime_error("Type Mismatch: Unary - on string");
        if (v.type == Value::Type::INT) return Value(-v.int_val);
        return Value(-v.num_val);
    }
    if (t.type == TokenType::NUMBER) {
        pos++;
        // &H..（16進）/ &B..（2進）リテラル。字句解析が原文のまま渡してくる
        if (t.text[0] == '&') {
            long v = strtol(t.text + 2, nullptr, (t.text[1] == 'H') ? 16 : 2);
            return Value((int)v);
        }
        float fv = (float)atof(t.text);
        bool has_dot = false;
        for (int i = 0; t.text[i]; i++) if (t.text[i] == '.') { has_dot = true; break; }
        if (!has_dot && fv >= -2147483648.0f && fv <= 2147483647.0f)
            return Value((int)(int)fv);
        return Value(fv);
    }
    if (t.type == TokenType::STRING) {
        pos++; return Value((const char*)t.text);
    }
    if (t.type == TokenType::IDENTIFIER) {
        char var_name[64];
        strncpy(var_name, t.text, sizeof(var_name)-1);
        var_name[sizeof(var_name)-1] = '\0';
        pos++;
        
        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::LPAREN) {
            pos++; 
            Value args[16];
            int arg_count = 0;
            args[arg_count++] = parse_relation(tokens, pos);
            while (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
                pos++;
                if (arg_count < 16) args[arg_count++] = parse_relation(tokens, pos);
                else { parse_relation(tokens, pos); arg_count++; } 
            }
            require_token(tokens, pos, TokenType::RPAREN, "Syntax Error: Expected ')' for function or array");
            pos++; 
            
            // DEF FN で定義したユーザー関数を優先して判定する。
            // `FN` で始まる名前は関数用に予約されており、未定義ならその旨を返す。
            if (var_name[0] == 'F' && var_name[1] == 'N' && var_name[2] != '\0') {
                if (!is_user_func(var_name)) throw std::runtime_error("Undefined function");
                if (arg_count != 1) throw std::runtime_error("FN takes exactly one argument");
                return call_user_func(var_name, args[0]);
            }

            if (is_builtin_function(var_name)) {
                return evaluate_builtin_function(var_name, args, arg_count);
            }

            ArrayRef* arr = get_array(var_name);
            if (!arr) throw std::runtime_error("Array not dimensioned");
            int flat_idx;
            if (arg_count == 1) {
                if (!args[0].is_numeric())
                    throw std::runtime_error("Type Mismatch: Array index must be numeric");
                flat_idx = flatten_array_index(arr, static_cast<int>(args[0].num_val));
            } else if (arg_count == 2) {
                if (!args[0].is_numeric() || !args[1].is_numeric())
                    throw std::runtime_error("Type Mismatch: Array index must be numeric");
                flat_idx = flatten_array_index(arr,
                    static_cast<int>(args[0].num_val),
                    static_cast<int>(args[1].num_val));
            } else {
                throw std::runtime_error("Syntax Error: Arrays support 1 or 2 dimensions");
            }
            return read_heap_value(arr->start_addr + (flat_idx * 8));
        }
        
        // TIMER は括弧なしの組み込み値（起動からの経過ミリ秒）
        if (strcmp(var_name, "TIMER") == 0) return Value((float)hal_system_millis());

        Value v_val;
        if (!get_variable(var_name, v_val)) {
            int len = strlen(var_name);
            if (len > 0 && var_name[len - 1] == '$') return Value("");
            return Value(0.0f);
        }
        return v_val;
    }
    if (t.type == TokenType::LPAREN) {
        pos++; 
        Value val = parse_relation(tokens, pos);
        require_token(tokens, pos, TokenType::RPAREN, "Missing closing parenthesis");
        pos++;
        return val;
    }
    throw std::runtime_error("Syntax Error in expression");
}

static Value parse_power_expr(const TokenList& tokens, int& pos) {
    Value val = parse_factor(tokens, pos);
    while (pos < tokens.size) {
        if (tokens.tokens[pos].type != TokenType::POWER) break;
        pos++;
        Value next_val = parse_factor(tokens, pos);
        if (val.type == Value::Type::STR || next_val.type == Value::Type::STR) throw std::runtime_error("Type Mismatch: Power on string");
        val.num_val = std::pow(val.num_val, next_val.num_val);
        val.type = Value::Type::NUM;
    }
    return val;
}

static Value parse_term(const TokenList& tokens, int& pos) {
    Value val = parse_power_expr(tokens, pos);
    while (pos < tokens.size) {
        TokenType op = tokens.tokens[pos].type;
        if (op != TokenType::MUL && op != TokenType::DIV) break;
        pos++;
        Value next_val = parse_power_expr(tokens, pos);
        
        if (val.type == Value::Type::STR || next_val.type == Value::Type::STR) {
            throw std::runtime_error("Type Mismatch: Cannot multiply/divide strings");
        }
        
        if (op == TokenType::MUL) {
            val.num_val *= next_val.num_val;
        } else {
            if (next_val.num_val == 0.0f) throw std::runtime_error("Division by zero");
            val.num_val /= next_val.num_val;
        }
        val.type = Value::Type::NUM;
    }
    return val;
}

// 整数除算 `\`。乗除より緩く、MOD より強い。両辺を整数に切り詰めて割る
static Value parse_intdiv_expr(const TokenList& tokens, int& pos) {
    Value val = parse_term(tokens, pos);
    while (pos < tokens.size && tokens.tokens[pos].type == TokenType::INTDIV) {
        pos++;
        Value rhs = parse_term(tokens, pos);
        int b = to_int_operand(rhs, "\\");
        if (b == 0) throw std::runtime_error("Division by zero");
        val = Value(to_int_operand(val, "\\") / b);
    }
    return val;
}

// 剰余 MOD。整数除算より緩く、加減より強い
static Value parse_mod_expr(const TokenList& tokens, int& pos) {
    Value val = parse_intdiv_expr(tokens, pos);
    while (pos < tokens.size && tokens.tokens[pos].type == TokenType::MOD_OP) {
        pos++;
        Value rhs = parse_intdiv_expr(tokens, pos);
        int b = to_int_operand(rhs, "MOD");
        if (b == 0) throw std::runtime_error("Division by zero");
        val = Value(to_int_operand(val, "MOD") % b);
    }
    return val;
}

Value parse_expression(const TokenList& tokens, int& pos) {
    Value val = parse_mod_expr(tokens, pos);
    while (pos < tokens.size) {
        TokenType op = tokens.tokens[pos].type;
        if (op != TokenType::PLUS && op != TokenType::MINUS) break;
        pos++;
        Value next_val = parse_mod_expr(tokens, pos);
        
        if (op == TokenType::PLUS) {
            if (val.type == Value::Type::STR && next_val.type == Value::Type::STR) {
                char buf[128];
                snprintf(buf, sizeof(buf), "%s%s", val.str_val, next_val.str_val);
                val = Value(buf);
            } else if (val.type != Value::Type::STR && next_val.type != Value::Type::STR) {
                val.num_val += next_val.num_val;
                val.type = Value::Type::NUM;
            } else {
                throw std::runtime_error("Type Mismatch: Cannot add string and number");
            }
        } else {
            if (val.type == Value::Type::STR || next_val.type == Value::Type::STR) throw std::runtime_error("Type Mismatch: Cannot subtract strings");
            val.num_val -= next_val.num_val;
            val.type = Value::Type::NUM;
        }
    }
    return val;
}

// 数値（INT/NUM）を int に切り詰める。ビット演算の被演算子に使う
static int to_int_operand(const Value& v, const char* op) {
    if (v.type == Value::Type::STR) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Type Mismatch: %s on string", op);
        throw std::runtime_error(msg);
    }
    return (v.type == Value::Type::INT) ? v.int_val : (int)v.num_val;
}

// 比較演算（= < > <= >= <>）。真は 1、偽は 0 を返す
static Value parse_comparison(const TokenList& tokens, int& pos) {
    Value val = parse_expression(tokens, pos);
    while (pos < tokens.size) {
        TokenType op = tokens.tokens[pos].type;
        if (op == TokenType::ASSIGN || op == TokenType::GT || op == TokenType::LT ||
            op == TokenType::GTE || op == TokenType::LTE || op == TokenType::NEQ) {
            pos++;
            Value next_val = parse_expression(tokens, pos);
            
            if ((val.type == Value::Type::STR) != (next_val.type == Value::Type::STR)) throw std::runtime_error("Type Mismatch: Cannot compare string and number");
            
            bool result = false;
            if (val.type != Value::Type::STR) {
                float a = val.num_val, b = next_val.num_val;
                if (op == TokenType::ASSIGN) result = (a == b);
                else if (op == TokenType::GT) result = (a > b);
                else if (op == TokenType::LT) result = (a < b);
                else if (op == TokenType::GTE) result = (a >= b);
                else if (op == TokenType::LTE) result = (a <= b);
                else if (op == TokenType::NEQ) result = (a != b);
            } else {
                const char* a = val.str_val, *b = next_val.str_val;
                int cmp = strcmp(a, b);
                if (op == TokenType::ASSIGN) result = (cmp == 0);
                else if (op == TokenType::GT) result = (cmp > 0);
                else if (op == TokenType::LT) result = (cmp < 0);
                else if (op == TokenType::GTE) result = (cmp >= 0);
                else if (op == TokenType::LTE) result = (cmp <= 0);
                else if (op == TokenType::NEQ) result = (cmp != 0);
            }
            val = Value(result ? 1.0f : 0.0f);
        } else break;
    }
    return val;
}

// NOT（単項・ビット補数）。比較より緩く、AND より強い
static Value parse_not_expr(const TokenList& tokens, int& pos) {
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::NOT) {
        pos++;
        Value v = parse_not_expr(tokens, pos); // 右結合
        return Value(~to_int_operand(v, "NOT"));
    }
    return parse_comparison(tokens, pos);
}

// AND（ビット積）。OR より強く、NOT より緩い
static Value parse_logical_and(const TokenList& tokens, int& pos) {
    Value val = parse_not_expr(tokens, pos);
    while (pos < tokens.size && tokens.tokens[pos].type == TokenType::AND) {
        pos++;
        Value rhs = parse_not_expr(tokens, pos);
        val = Value(to_int_operand(val, "AND") & to_int_operand(rhs, "AND"));
    }
    return val;
}

// OR（ビット和）。AND より緩く、XOR より強い
static Value parse_bit_or(const TokenList& tokens, int& pos) {
    Value val = parse_logical_and(tokens, pos);
    while (pos < tokens.size && tokens.tokens[pos].type == TokenType::OR) {
        pos++;
        Value rhs = parse_logical_and(tokens, pos);
        val = Value(to_int_operand(val, "OR") | to_int_operand(rhs, "OR"));
    }
    return val;
}

// 式全体の入口。XOR が最も優先順位が低い（MS BASIC 系と同じ並び）。
//   優先順位: XOR < OR < AND < NOT < 比較 < +/- < MOD < \ < */ < ^ < 単項-
// AND/OR/NOT/XOR はビット演算。比較は 1/0 を返すので論理積・論理和としても使える
// （NOT はビット補数なので、論理否定は `X=0` のように比較で書くこと）。
Value parse_relation(const TokenList& tokens, int& pos) {
    Value val = parse_bit_or(tokens, pos);
    while (pos < tokens.size && tokens.tokens[pos].type == TokenType::XOR) {
        pos++;
        Value rhs = parse_bit_or(tokens, pos);
        val = Value(to_int_operand(val, "XOR") ^ to_int_operand(rhs, "XOR"));
    }
    return val;
}

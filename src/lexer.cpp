#include "lexer.h"
#include <cctype>
#include <cstring>
#include <cstdio>
#include <stdexcept>

// ---------------------------------------------------------
// キーワード表。新しい命令はここに 1 行足すだけでよい。
// 別名（GET@ / GET_AT など）は同じ TokenType で複数行登録する。
// ---------------------------------------------------------
struct Keyword {
    const char* name;
    TokenType   type;
};

static const Keyword KEYWORDS[] = {
    {"PRINT", TokenType::PRINT},   {"LET", TokenType::LET},
    {"GOTO", TokenType::GOTO},     {"GOSUB", TokenType::GOSUB},
    {"RETURN", TokenType::RETURN}, {"IF", TokenType::IF},
    {"THEN", TokenType::THEN},     {"ELSEIF", TokenType::ELSEIF},
    {"ELSE", TokenType::ELSE},     {"FOR", TokenType::FOR},
    {"TO", TokenType::TO},         {"STEP", TokenType::STEP},
    {"NEXT", TokenType::NEXT},     {"NEW", TokenType::NEW},
    {"LIST", TokenType::LIST},     {"RUN", TokenType::RUN},
    {"READ", TokenType::READ},     {"DATA", TokenType::DATA},
    {"RESTORE", TokenType::RESTORE}, {"DIM", TokenType::DIM},
    {"INPUT", TokenType::INPUT},   {"END", TokenType::END},
    {"STOP", TokenType::STOP},     {"INIT", TokenType::INIT},
    {"CLEAR", TokenType::CLEAR},   {"NEWON", TokenType::NEWON},
    {"WIDTH", TokenType::WIDTH},   {"CONSOLE", TokenType::CONSOLE},
    {"CLS", TokenType::CLS},       {"LOCATE", TokenType::LOCATE},
    {"AUTO", TokenType::AUTO},     {"AND", TokenType::AND},
    {"OR", TokenType::OR},         {"NOT", TokenType::NOT},
    {"XOR", TokenType::XOR},       {"MOD", TokenType::MOD_OP},
    {"DEF", TokenType::DEF},       {"POKE", TokenType::POKE},
    {"REPEAT", TokenType::REPEAT}, {"UNTIL", TokenType::UNTIL},
    {"GET", TokenType::GET},       {"FILES", TokenType::FILES},
    {"SAVE", TokenType::SAVE},     {"LOAD", TokenType::LOAD},
    {"OPEN", TokenType::OPEN},     {"CLOSE", TokenType::CLOSE},
    {"RENUM", TokenType::RENUM},   {"DELETE", TokenType::DELETE_CMD},
    {"CONT", TokenType::CONT},     {"TRON", TokenType::TRON},
    {"TROFF", TokenType::TROFF},   {"WHILE", TokenType::WHILE},
    {"RANDOMIZE", TokenType::RANDOMIZE},
    {"WEND", TokenType::WEND},     {"USING", TokenType::USING},
    {"RESUME", TokenType::RESUME},
    {"KILL", TokenType::KILL},     {"NAME", TokenType::NAME},
    {"AS", TokenType::AS},         {"ON", TokenType::ON},
    {"GPIO", TokenType::GPIO},     {"WINDOW", TokenType::WINDOW},
    {"PSET", TokenType::PSET},     {"LINE", TokenType::LINE},
    {"CIRCLE", TokenType::CIRCLE}, {"POLY", TokenType::POLY},
    {"PAINT", TokenType::PAINT},
    // マニュアル記載の `GET@` / `PUT@` が本来の書式。
    // `GET_AT` / `PUT_AT` は既存プログラム互換のために残す
    {"GET@", TokenType::GET_AT},   {"GET_AT", TokenType::GET_AT},
    {"PUT@", TokenType::PUT_AT},   {"PUT_AT", TokenType::PUT_AT},
    {"COLOR", TokenType::COLOR},   {"BRIGHTNESS", TokenType::BRIGHTNESS},
    {"WAIT", TokenType::WAIT},     {"BEEP", TokenType::BEEP},
    {"PLAY", TokenType::PLAY},     {"MUSIC", TokenType::MUSIC},
    {"SOUND", TokenType::SOUND},   {"REM", TokenType::REM},
};

// 組み込み関数名。キーワードではないが、変数名の検査（8 文字制限など）を通さない
static const char* const BUILTIN_NAMES[] = {
    "ABS", "INT", "RND", "SGN", "SQR", "SIN", "COS", "TAN", "LOG", "EXP",
    "LEN", "MID$", "LEFT$", "RIGHT$", "CHR$", "ASC", "VAL", "STR$",
    "TAB", "PEEK", "TOUCH",
    "INSTR", "STRING$", "SPACE$", "HEX$", "POINT", "EOF", "BATTERY",
    "PIN", "ADIN", "ACCEL", "GYRO",
};

static bool lookup_keyword(const char* name, TokenType& out) {
    for (const Keyword& k : KEYWORDS) {
        if (strcmp(name, k.name) == 0) { out = k.type; return true; }
    }
    return false;
}

static bool is_builtin_name(const char* name) {
    for (const char* b : BUILTIN_NAMES) {
        if (strcmp(name, b) == 0) return true;
    }
    return false;
}

TokenList lex(const char* source) {
    TokenList token_list;
    int pos = 0;
    int len = strlen(source);

    while (pos < len) {
        if (token_list.size >= MAX_TOKENS_PER_LINE) break;

        char c = source[pos];

        if (std::isspace((unsigned char)c)) {
            pos++;
            continue;
        }

        if (c == '<' && pos + 1 < len && source[pos+1] == '>') {
            token_list.tokens[token_list.size].type = TokenType::NEQ;
            strcpy(token_list.tokens[token_list.size].text, "<>");
            token_list.size++;
            pos += 2;
            continue;
        }
        if (c == '<' && pos + 1 < len && source[pos+1] == '=') {
            token_list.tokens[token_list.size].type = TokenType::LTE;
            strcpy(token_list.tokens[token_list.size].text, "<=");
            token_list.size++;
            pos += 2;
            continue;
        }
        if (c == '>' && pos + 1 < len && source[pos+1] == '=') {
            token_list.tokens[token_list.size].type = TokenType::GTE;
            strcpy(token_list.tokens[token_list.size].text, ">=");
            token_list.size++;
            pos += 2;
            continue;
        }

        if (c == '=') { token_list.tokens[token_list.size] = {TokenType::ASSIGN, "="}; token_list.size++; pos++; continue; }
        if (c == '+') { token_list.tokens[token_list.size] = {TokenType::PLUS, "+"}; token_list.size++; pos++; continue; }
        if (c == '-') { token_list.tokens[token_list.size] = {TokenType::MINUS, "-"}; token_list.size++; pos++; continue; }
        if (c == '*') {
            // `*NAME` は行ラベル、それ以外は乗算。直前トークンが値（識別子・数値・
            // 文字列・")"）なら乗算、そうでなければ（行頭・GOTO/THEN の直後など）ラベル。
            bool prev_is_operand = false;
            if (token_list.size > 0) {
                TokenType pt = token_list.tokens[token_list.size - 1].type;
                // 先頭の NUMBER は行番号（`40 *LOOP` の 40）なので値とはみなさない。
                // それ以外の NUMBER/識別子/文字列/")" が直前なら乗算。
                bool number_is_line_no = (pt == TokenType::NUMBER && token_list.size == 1);
                prev_is_operand = (pt == TokenType::IDENTIFIER || pt == TokenType::STRING ||
                                   pt == TokenType::RPAREN ||
                                   (pt == TokenType::NUMBER && !number_is_line_no));
            }
            if (!prev_is_operand && pos + 1 < len &&
                (std::isalpha((unsigned char)source[pos+1]) || source[pos+1] == '_')) {
                int start = pos;      // '*' を含めて取り込む
                pos++;                // '*' を飛ばす
                while (pos < len && (std::isalnum((unsigned char)source[pos]) || source[pos] == '_')) pos++;
                int name_len = pos - start;
                if (name_len >= MAX_TOKEN_LEN) name_len = MAX_TOKEN_LEN - 1;
                Token t;
                t.type = TokenType::LABEL;
                memcpy(t.text, source + start, name_len);
                t.text[name_len] = '\0';
                // ラベル名は大文字化して比較を大小無視にする（他のキーワードと同様）
                for (int i = 1; i < name_len; i++) t.text[i] = std::toupper((unsigned char)t.text[i]);
                token_list.tokens[token_list.size++] = t;
                continue;
            }
            token_list.tokens[token_list.size] = {TokenType::MUL, "*"}; token_list.size++; pos++; continue;
        }
        if (c == '/') { token_list.tokens[token_list.size] = {TokenType::DIV, "/"}; token_list.size++; pos++; continue; }
        if (c == '\\') { token_list.tokens[token_list.size] = {TokenType::INTDIV, "\\"}; token_list.size++; pos++; continue; }
        if (c == '^') { token_list.tokens[token_list.size] = {TokenType::POWER, "^"}; token_list.size++; pos++; continue; }
        if (c == '(') { token_list.tokens[token_list.size] = {TokenType::LPAREN, "("}; token_list.size++; pos++; continue; }
        if (c == ')') { token_list.tokens[token_list.size] = {TokenType::RPAREN, ")"}; token_list.size++; pos++; continue; }
        if (c == '>') { token_list.tokens[token_list.size] = {TokenType::GT, ">"}; token_list.size++; pos++; continue; }
        if (c == '<') { token_list.tokens[token_list.size] = {TokenType::LT, "<"}; token_list.size++; pos++; continue; }
        if (c == ',') { token_list.tokens[token_list.size] = {TokenType::COMMA, ","}; token_list.size++; pos++; continue; }
        if (c == ';') { token_list.tokens[token_list.size] = {TokenType::SEMICOLON, ";"}; token_list.size++; pos++; continue; }
        if (c == ':') { token_list.tokens[token_list.size] = {TokenType::COLON, ":"}; token_list.size++; pos++; continue; }
        if (c == '#') { token_list.tokens[token_list.size] = {TokenType::HASH, "#"}; token_list.size++; pos++; continue; }

        if (c == '"') {
            pos++;
            int start = pos;
            while (pos < len && source[pos] != '"') pos++;
            
            Token t;
            t.type = TokenType::STRING;
            int text_len = pos - start;
            if (text_len >= MAX_TOKEN_LEN) text_len = MAX_TOKEN_LEN - 1;
            strncpy(t.text, source + start, text_len);
            t.text[text_len] = '\0';
            token_list.tokens[token_list.size++] = t;
            
            if (pos < len && source[pos] == '"') pos++;
            continue;
        }

        if (std::isdigit((unsigned char)c)) {
            int start = pos;
            bool has_dot = false;
            while (pos < len && (std::isdigit((unsigned char)source[pos]) || source[pos] == '.')) {
                if (source[pos] == '.') {
                    if (has_dot) break;
                    has_dot = true;
                }
                pos++;
            }
            Token t;
            t.type = TokenType::NUMBER;
            int text_len = pos - start;
            if (text_len >= MAX_TOKEN_LEN) text_len = MAX_TOKEN_LEN - 1;
            strncpy(t.text, source + start, text_len);
            t.text[text_len] = '\0';
            token_list.tokens[token_list.size++] = t;
            continue;
        }

        if (std::isalpha((unsigned char)c)) {
            int start = pos;
            while (pos < len && (std::isalnum((unsigned char)source[pos]) || source[pos] == '$' || source[pos] == '%' || source[pos] == '#' || source[pos] == '@' || source[pos] == '_')) {
                pos++;
            }
            char ident[MAX_TOKEN_LEN];
            int text_len = pos - start;
            if (text_len >= MAX_TOKEN_LEN) text_len = MAX_TOKEN_LEN - 1;
            strncpy(ident, source + start, text_len);
            ident[text_len] = '\0';

            // Convert to uppercase for keywords
            char upper_ident[MAX_TOKEN_LEN];
            for (int i = 0; i <= text_len; ++i) upper_ident[i] = std::toupper(ident[i]);

            Token t;
            t.type = TokenType::IDENTIFIER;
            strncpy(t.text, upper_ident, MAX_TOKEN_LEN);

            TokenType kw;
            if (lookup_keyword(upper_ident, kw)) {
                t.type = kw;
                if (kw == TokenType::REM) {
                    // REM 以降は行末までコメント。本文を text に取り込んで行の字句解析を終える
                    int s = pos;
                    while (s < len && (source[s] == ' ' || source[s] == '\t')) s++;
                    int clen = len - s;
                    if (clen >= MAX_TOKEN_LEN) clen = MAX_TOKEN_LEN - 1;
                    if (clen < 0) clen = 0;
                    memcpy(t.text, source + s, clen);
                    t.text[clen] = '\0';
                    token_list.tokens[token_list.size++] = t;
                    break; // この行はこれ以上読まない
                }
            }
            else if (is_builtin_name(upper_ident)) {} // 組み込み関数名は変数名検査を通さない
            else {
                // Variable name rules: [A-Z][A-Z0-9]*[%$]? with max 8 characters total
                // Examples: A, B$, X0%, SCORE, NAME$, COUNT%
                bool valid = (text_len >= 1 && text_len <= 8);
                if (valid) {
                    if (!std::isalpha(upper_ident[0])) valid = false;
                    // Determine where the base name ends (before optional sigil)
                    int base_end = (int)text_len;
                    if (valid && text_len > 0 &&
                        (upper_ident[text_len - 1] == '$' || upper_ident[text_len - 1] == '%' || upper_ident[text_len - 1] == '#')) {
                        base_end = (int)text_len - 1;
                        if (base_end == 0) valid = false; // bare sigil alone is invalid
                    }
                    // All non-sigil characters after the first must be alphanumeric
                    for (int k = 1; k < base_end && valid; k++) {
                        if (!std::isalnum(upper_ident[k])) valid = false;
                    }
                }
                if (!valid) {
                    throw std::runtime_error("Syntax Error: Invalid variable name");
                }
            }

            token_list.tokens[token_list.size++] = t;
            continue;
        }

        // ' 以降は行末までコメント（REM と同じ扱い）
        if (c == '\'') {
            Token t;
            t.type = TokenType::REM;
            int s = pos + 1;
            while (s < len && (source[s] == ' ' || source[s] == '\t')) s++;
            int clen = len - s;
            if (clen >= MAX_TOKEN_LEN) clen = MAX_TOKEN_LEN - 1;
            if (clen < 0) clen = 0;
            memcpy(t.text, source + s, clen);
            t.text[clen] = '\0';
            token_list.tokens[token_list.size++] = t;
            break; // この行はこれ以上読まない
        }

        // ? は PRINT のショートカット
        if (c == '?') {
            token_list.tokens[token_list.size] = {TokenType::PRINT, "?"};
            token_list.size++;
            pos++;
            continue;
        }

        // &H（16進）/ &B（2進）リテラル。NUMBER トークンとして原文のまま保持し、
        // 値への変換は式評価（parse_factor）で行う。LIST でも &H 表記が残る
        if (c == '&' && pos + 1 < len) {
            char kind = std::toupper((unsigned char)source[pos + 1]);
            if (kind == 'H' || kind == 'B') {
                Token t;
                t.type = TokenType::NUMBER;
                int tp = 0;
                t.text[tp++] = '&';
                t.text[tp++] = kind;
                int s = pos + 2;
                while (s < len && tp < MAX_TOKEN_LEN - 1) {
                    char d = std::toupper((unsigned char)source[s]);
                    bool ok = (kind == 'H') ? std::isxdigit((unsigned char)d) : (d == '0' || d == '1');
                    if (!ok) break;
                    t.text[tp++] = d;
                    s++;
                }
                if (tp == 2) throw std::runtime_error("Syntax Error: Invalid &-literal");
                t.text[tp] = '\0';
                token_list.tokens[token_list.size++] = t;
                pos = s;
                continue;
            }
        }

        // Unknown character, skip it
        pos++;
    }

    if (token_list.size < MAX_TOKENS_PER_LINE) {
        token_list.tokens[token_list.size++] = {TokenType::END_OF_FILE, ""};
    } else {
        token_list.tokens[MAX_TOKENS_PER_LINE - 1] = {TokenType::END_OF_FILE, ""};
    }

    return token_list;
}

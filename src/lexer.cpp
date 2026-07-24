#include "lexer.h"
#include <cctype>
#include <cstring>
#include <cstdio>
#include <stdexcept>

TokenList lex(const char* source) {
    TokenList token_list;
    int pos = 0;
    int len = strlen(source);

    while (pos < len) {
        if (token_list.size >= MAX_TOKENS_PER_LINE) break;

        char c = source[pos];

        if (std::isspace(c)) {
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
        if (c == '^') { token_list.tokens[token_list.size] = {TokenType::POWER, "^"}; token_list.size++; pos++; continue; }
        if (c == '(') { token_list.tokens[token_list.size] = {TokenType::LPAREN, "("}; token_list.size++; pos++; continue; }
        if (c == ')') { token_list.tokens[token_list.size] = {TokenType::RPAREN, ")"}; token_list.size++; pos++; continue; }
        if (c == '>') { token_list.tokens[token_list.size] = {TokenType::GT, ">"}; token_list.size++; pos++; continue; }
        if (c == '<') { token_list.tokens[token_list.size] = {TokenType::LT, "<"}; token_list.size++; pos++; continue; }
        if (c == ',') { token_list.tokens[token_list.size] = {TokenType::COMMA, ","}; token_list.size++; pos++; continue; }
        if (c == ';') { token_list.tokens[token_list.size] = {TokenType::SEMICOLON, ";"}; token_list.size++; pos++; continue; }
        if (c == ':') { token_list.tokens[token_list.size] = {TokenType::COLON, ":"}; token_list.size++; pos++; continue; }

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

        if (std::isdigit(c)) {
            int start = pos;
            bool has_dot = false;
            while (pos < len && (std::isdigit(source[pos]) || source[pos] == '.')) {
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

        if (std::isalpha(c)) {
            int start = pos;
            while (pos < len && (std::isalnum(source[pos]) || source[pos] == '$' || source[pos] == '%' || source[pos] == '@' || source[pos] == '_')) {
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

            if (strcmp(upper_ident, "PRINT") == 0) t.type = TokenType::PRINT;
            else if (strcmp(upper_ident, "LET") == 0) t.type = TokenType::LET;
            else if (strcmp(upper_ident, "GOTO") == 0) t.type = TokenType::GOTO;
            else if (strcmp(upper_ident, "GOSUB") == 0) t.type = TokenType::GOSUB;
            else if (strcmp(upper_ident, "RETURN") == 0) t.type = TokenType::RETURN;
            else if (strcmp(upper_ident, "IF") == 0) t.type = TokenType::IF;
            else if (strcmp(upper_ident, "THEN") == 0) t.type = TokenType::THEN;
            else if (strcmp(upper_ident, "ELSEIF") == 0) t.type = TokenType::ELSEIF;
            else if (strcmp(upper_ident, "ELSE") == 0) t.type = TokenType::ELSE;
            else if (strcmp(upper_ident, "FOR") == 0) t.type = TokenType::FOR;
            else if (strcmp(upper_ident, "TO") == 0) t.type = TokenType::TO;
            else if (strcmp(upper_ident, "STEP") == 0) t.type = TokenType::STEP;
            else if (strcmp(upper_ident, "NEXT") == 0) t.type = TokenType::NEXT;
            else if (strcmp(upper_ident, "NEW") == 0) t.type = TokenType::NEW;
            else if (strcmp(upper_ident, "LIST") == 0) t.type = TokenType::LIST;
            else if (strcmp(upper_ident, "RUN") == 0) t.type = TokenType::RUN;
            else if (strcmp(upper_ident, "READ") == 0) t.type = TokenType::READ;
            else if (strcmp(upper_ident, "DATA") == 0) t.type = TokenType::DATA;
            else if (strcmp(upper_ident, "RESTORE") == 0) t.type = TokenType::RESTORE;
            else if (strcmp(upper_ident, "DIM") == 0) t.type = TokenType::DIM;
            else if (strcmp(upper_ident, "INPUT") == 0) t.type = TokenType::INPUT;
            else if (strcmp(upper_ident, "END") == 0) t.type = TokenType::END;
            else if (strcmp(upper_ident, "STOP") == 0) t.type = TokenType::STOP;
            else if (strcmp(upper_ident, "INIT") == 0) t.type = TokenType::INIT;
            else if (strcmp(upper_ident, "CLEAR") == 0) t.type = TokenType::CLEAR;
            else if (strcmp(upper_ident, "NEWON") == 0) t.type = TokenType::NEWON;
            else if (strcmp(upper_ident, "WIDTH") == 0) t.type = TokenType::WIDTH;
            else if (strcmp(upper_ident, "CONSOLE") == 0) t.type = TokenType::CONSOLE;
            else if (strcmp(upper_ident, "CLS") == 0) t.type = TokenType::CLS;
            else if (strcmp(upper_ident, "LOCATE") == 0) t.type = TokenType::LOCATE;
            else if (strcmp(upper_ident, "REPEAT") == 0) t.type = TokenType::REPEAT;
            else if (strcmp(upper_ident, "UNTIL") == 0) t.type = TokenType::UNTIL;
            else if (strcmp(upper_ident, "GET") == 0) t.type = TokenType::GET;
            else if (strcmp(upper_ident, "FILES") == 0) t.type = TokenType::FILES;
            else if (strcmp(upper_ident, "SAVE") == 0) t.type = TokenType::SAVE;
            else if (strcmp(upper_ident, "LOAD") == 0) t.type = TokenType::LOAD;
            else if (strcmp(upper_ident, "KILL") == 0) t.type = TokenType::KILL;
            else if (strcmp(upper_ident, "NAME") == 0) t.type = TokenType::NAME;
            else if (strcmp(upper_ident, "AS") == 0) t.type = TokenType::AS;
            else if (strcmp(upper_ident, "ON") == 0) t.type = TokenType::ON;
            else if (strcmp(upper_ident, "GPIO") == 0) t.type = TokenType::GPIO;
            else if (strcmp(upper_ident, "WINDOW") == 0) t.type = TokenType::WINDOW;
            else if (strcmp(upper_ident, "PSET") == 0) t.type = TokenType::PSET;
            else if (strcmp(upper_ident, "LINE") == 0) t.type = TokenType::LINE;
            else if (strcmp(upper_ident, "CIRCLE") == 0) t.type = TokenType::CIRCLE;
            else if (strcmp(upper_ident, "POLY") == 0) t.type = TokenType::POLY;
            else if (strcmp(upper_ident, "PAINT") == 0) t.type = TokenType::PAINT;
            // マニュアル記載の `GET@` / `PUT@` が本来の書式。
            // `GET_AT` / `PUT_AT` は既存プログラム互換のために残す
            else if (strcmp(upper_ident, "GET@") == 0 ||
                     strcmp(upper_ident, "GET_AT") == 0) t.type = TokenType::GET_AT;
            else if (strcmp(upper_ident, "PUT@") == 0 ||
                     strcmp(upper_ident, "PUT_AT") == 0) t.type = TokenType::PUT_AT;
            else if (strcmp(upper_ident, "COLOR") == 0) t.type = TokenType::COLOR;
            else if (strcmp(upper_ident, "BRIGHTNESS") == 0) t.type = TokenType::BRIGHTNESS;
            else if (strcmp(upper_ident, "WAIT") == 0) t.type = TokenType::WAIT;
            else if (strcmp(upper_ident, "BEEP") == 0) t.type = TokenType::BEEP;
            else if (strcmp(upper_ident, "PLAY") == 0) t.type = TokenType::PLAY;
            else if (strcmp(upper_ident, "MUSIC") == 0) t.type = TokenType::MUSIC;
            else if (strcmp(upper_ident, "SOUND") == 0) t.type = TokenType::SOUND;
            else if (strcmp(upper_ident, "ABS") == 0 || strcmp(upper_ident, "INT") == 0 || strcmp(upper_ident, "RND") == 0 ||
                     strcmp(upper_ident, "SGN") == 0 || strcmp(upper_ident, "SQR") == 0 ||
                     strcmp(upper_ident, "SIN") == 0 || strcmp(upper_ident, "COS") == 0 || strcmp(upper_ident, "TAN") == 0 ||
                     strcmp(upper_ident, "LOG") == 0 || strcmp(upper_ident, "EXP") == 0 ||
                     strcmp(upper_ident, "LEN") == 0 || strcmp(upper_ident, "MID$") == 0 || 
                     strcmp(upper_ident, "LEFT$") == 0 || strcmp(upper_ident, "RIGHT$") == 0 ||
                     strcmp(upper_ident, "CHR$") == 0 || strcmp(upper_ident, "ASC") == 0 ||
                     strcmp(upper_ident, "VAL") == 0 || strcmp(upper_ident, "STR$") == 0 ||
                     strcmp(upper_ident, "TAB") == 0 ||
                     strcmp(upper_ident, "TOUCH") == 0) {} // Built-in functions bypass variable checks
            else {
                // Variable name rules: [A-Z][A-Z0-9]*[%$]? with max 8 characters total
                // Examples: A, B$, X0%, SCORE, NAME$, COUNT%
                bool valid = (text_len >= 1 && text_len <= 8);
                if (valid) {
                    if (!std::isalpha(upper_ident[0])) valid = false;
                    // Determine where the base name ends (before optional sigil)
                    int base_end = (int)text_len;
                    if (valid && text_len > 0 &&
                        (upper_ident[text_len - 1] == '$' || upper_ident[text_len - 1] == '%')) {
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

        // ? は PRINT のショートカット
        if (c == '?') {
            token_list.tokens[token_list.size] = {TokenType::PRINT, "?"};
            token_list.size++;
            pos++;
            continue;
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

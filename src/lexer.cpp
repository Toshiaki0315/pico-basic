#include "lexer.h"
#include "strutil.h"
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
    {"SYNC", TokenType::SYNC},
    {"POWEROFF", TokenType::POWEROFF},
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


// ---------------------------------------------------------
// 字句解析の本体
//
// 1 文字目で担当が決まるので、種類ごとの小さな関数に分け、lex() は順に
// 試すだけにしてある。どの関数も「自分の担当なら pos を進めてトークンを
// 積み、そうでなければ NotMine を返す」という同じ約束で書く。
// ---------------------------------------------------------

// 各規則の戻り値
enum class LexStep {
    NotMine,   // 担当ではない。次の規則を試す
    Consumed,  // トークンを読んだ
    EndOfLine, // 行末まで読み切った（コメント）。この行はここで終わり
};

using LexRule = LexStep (*)(const char* src, int len, int& pos, TokenList& out);

// 本文を持たないトークンを積む
static void push_token(TokenList& out, TokenType type, const char* text) {
    out.tokens[out.size].type = type;
    copy_string(out.tokens[out.size].text, MAX_TOKEN_LEN, text);
    out.size++;
}

// 元文字列の [start, end) を本文として積む。
// 元文字列は終端が無い範囲なので、strncpy ではなく長さを決めて memcpy する
static void push_slice(TokenList& out, TokenType type, const char* src, int start, int end) {
    int text_len = end - start;
    if (text_len < 0) text_len = 0;
    if (text_len >= MAX_TOKEN_LEN) text_len = MAX_TOKEN_LEN - 1;

    Token t;
    t.type = type;
    memcpy(t.text, src + start, text_len);
    t.text[text_len] = '\0';
    out.tokens[out.size++] = t;
}

// REM / ' に続く行末までを本文として積む。先頭の空白は落とす
static void push_comment(TokenList& out, TokenType type, const char* src, int len, int from) {
    while (from < len && (src[from] == ' ' || src[from] == '\t')) from++;
    push_slice(out, type, src, from, len);
}

// ---- 演算子・区切り ----

static LexStep lex_operator(const char* src, int len, int& pos, TokenList& out) {
    // 2 文字のものを先に見る（`<` を先に取ると `<=` が壊れる）
    if (pos + 1 < len) {
        const char* two = src + pos;
        TokenType type = TokenType::END_OF_FILE;
        if (two[0] == '<' && two[1] == '>') type = TokenType::NEQ;
        else if (two[0] == '<' && two[1] == '=') type = TokenType::LTE;
        else if (two[0] == '>' && two[1] == '=') type = TokenType::GTE;
        if (type != TokenType::END_OF_FILE) {
            char text[3] = { two[0], two[1], '\0' };
            push_token(out, type, text);
            pos += 2;
            return LexStep::Consumed;
        }
    }

    struct Single { char ch; TokenType type; };
    static const Single SINGLES[] = {
        {'=', TokenType::ASSIGN},    {'+', TokenType::PLUS},
        {'-', TokenType::MINUS},     {'*', TokenType::MUL},
        {'/', TokenType::DIV},       {'\\', TokenType::INTDIV},
        {'^', TokenType::POWER},     {'(', TokenType::LPAREN},
        {')', TokenType::RPAREN},    {'>', TokenType::GT},
        {'<', TokenType::LT},        {',', TokenType::COMMA},
        {';', TokenType::SEMICOLON}, {':', TokenType::COLON},
        {'#', TokenType::HASH},
        {'?', TokenType::PRINT}, // ? は PRINT のショートカット
    };
    for (const Single& s : SINGLES) {
        if (src[pos] != s.ch) continue;
        char text[2] = { s.ch, '\0' };
        push_token(out, s.type, text);
        pos++;
        return LexStep::Consumed;
    }
    return LexStep::NotMine;
}

// ---- 行ラベル `*NAME` ----

// 直前のトークンが値なら `*` は乗算。行頭や GOTO / THEN の直後ならラベル
static bool previous_token_is_operand(const TokenList& out) {
    if (out.size == 0) return false;
    TokenType pt = out.tokens[out.size - 1].type;
    // 先頭の NUMBER は行番号（`40 *LOOP` の 40）なので値とはみなさない
    bool number_is_line_no = (pt == TokenType::NUMBER && out.size == 1);
    return pt == TokenType::IDENTIFIER || pt == TokenType::STRING ||
           pt == TokenType::RPAREN ||
           (pt == TokenType::NUMBER && !number_is_line_no);
}

// 乗算より先に試すこと（どちらも '*' で始まる）
static LexStep lex_label(const char* src, int len, int& pos, TokenList& out) {
    if (src[pos] != '*') return LexStep::NotMine;
    if (previous_token_is_operand(out)) return LexStep::NotMine;
    if (pos + 1 >= len) return LexStep::NotMine;
    if (!std::isalpha((unsigned char)src[pos + 1]) && src[pos + 1] != '_') return LexStep::NotMine;

    int start = pos; // '*' を含めて取り込む
    pos++;
    while (pos < len && (std::isalnum((unsigned char)src[pos]) || src[pos] == '_')) pos++;

    push_slice(out, TokenType::LABEL, src, start, pos);
    // ラベル名は大文字化して比較を大小無視にする（他のキーワードと同様）
    char* text = out.tokens[out.size - 1].text;
    for (int i = 1; text[i] != '\0'; i++) text[i] = (char)std::toupper((unsigned char)text[i]);
    return LexStep::Consumed;
}

// ---- 文字列リテラル ----

static LexStep lex_string(const char* src, int len, int& pos, TokenList& out) {
    if (src[pos] != '"') return LexStep::NotMine;

    pos++;
    int start = pos;
    while (pos < len && src[pos] != '"') pos++;
    push_slice(out, TokenType::STRING, src, start, pos);
    if (pos < len && src[pos] == '"') pos++; // 閉じ引用符
    return LexStep::Consumed;
}

// ---- 数値 ----

static LexStep lex_number(const char* src, int len, int& pos, TokenList& out) {
    if (!std::isdigit((unsigned char)src[pos])) return LexStep::NotMine;

    int start = pos;
    bool has_dot = false;
    while (pos < len && (std::isdigit((unsigned char)src[pos]) || src[pos] == '.')) {
        if (src[pos] == '.') {
            if (has_dot) break; // 2 つ目の '.' は別のトークン
            has_dot = true;
        }
        pos++;
    }
    push_slice(out, TokenType::NUMBER, src, start, pos);
    return LexStep::Consumed;
}

// &H（16進）/ &B（2進）リテラル。NUMBER トークンとして原文のまま保持し、
// 値への変換は式評価（parse_factor）で行う。LIST でも &H 表記が残る
static LexStep lex_radix_number(const char* src, int len, int& pos, TokenList& out) {
    if (src[pos] != '&' || pos + 1 >= len) return LexStep::NotMine;
    char kind = (char)std::toupper((unsigned char)src[pos + 1]);
    if (kind != 'H' && kind != 'B') return LexStep::NotMine;

    Token t;
    t.type = TokenType::NUMBER;
    int tp = 0;
    t.text[tp++] = '&';
    t.text[tp++] = kind;

    int s = pos + 2;
    while (s < len && tp < MAX_TOKEN_LEN - 1) {
        char d = (char)std::toupper((unsigned char)src[s]);
        bool ok = (kind == 'H') ? (std::isxdigit((unsigned char)d) != 0) : (d == '0' || d == '1');
        if (!ok) break;
        t.text[tp++] = d;
        s++;
    }
    if (tp == 2) throw std::runtime_error("Syntax Error: Invalid &-literal");

    t.text[tp] = '\0';
    out.tokens[out.size++] = t;
    pos = s;
    return LexStep::Consumed;
}

// ---- 語（キーワード・組み込み関数名・変数名）----

// 変数名の規則: [A-Z][A-Z0-9]*[$%#]? で 8 文字以内（例: A, B$, X0%, SCORE）
static void check_variable_name(const char* upper, int text_len) {
    bool valid = (text_len >= 1 && text_len <= 8);
    if (valid && !std::isalpha((unsigned char)upper[0])) valid = false;

    if (valid) {
        // 型を表す末尾 1 文字は名前の一部として数えない
        int base_end = text_len;
        char last = upper[text_len - 1];
        if (last == '$' || last == '%' || last == '#') {
            base_end = text_len - 1;
            if (base_end == 0) valid = false; // 記号だけの名前は無効
        }
        for (int k = 1; k < base_end && valid; k++) {
            if (!std::isalnum((unsigned char)upper[k])) valid = false;
        }
    }

    if (!valid) throw std::runtime_error("Syntax Error: Invalid variable name");
}

static LexStep lex_word(const char* src, int len, int& pos, TokenList& out) {
    if (!std::isalpha((unsigned char)src[pos])) return LexStep::NotMine;

    int start = pos;
    while (pos < len && (std::isalnum((unsigned char)src[pos]) || src[pos] == '$' ||
                         src[pos] == '%' || src[pos] == '#' || src[pos] == '@' ||
                         src[pos] == '_')) {
        pos++;
    }

    int text_len = pos - start;
    if (text_len >= MAX_TOKEN_LEN) text_len = MAX_TOKEN_LEN - 1;
    char upper[MAX_TOKEN_LEN];
    for (int i = 0; i < text_len; i++) upper[i] = (char)std::toupper((unsigned char)src[start + i]);
    upper[text_len] = '\0';

    TokenType kw;
    if (lookup_keyword(upper, kw)) {
        if (kw == TokenType::REM) {
            // REM 以降は行末までコメント。本文を text に取り込んで行を終える
            push_comment(out, TokenType::REM, src, len, pos);
            return LexStep::EndOfLine;
        }
        push_token(out, kw, upper);
        return LexStep::Consumed;
    }

    // 組み込み関数名は変数名の検査を通さない（8 文字制限などに引っかかるため）
    if (!is_builtin_name(upper)) check_variable_name(upper, text_len);

    push_token(out, TokenType::IDENTIFIER, upper);
    return LexStep::Consumed;
}

// ' 以降は行末までコメント（REM と同じ扱い）
static LexStep lex_line_comment(const char* src, int len, int& pos, TokenList& out) {
    if (src[pos] != '\'') return LexStep::NotMine;
    push_comment(out, TokenType::REM, src, len, pos + 1);
    return LexStep::EndOfLine;
}

// ---- 本体 ----

TokenList lex(const char* source) {
    // 試す順。ラベルは乗算より先に置くこと（どちらも '*' で始まる）
    static const LexRule RULES[] = {
        lex_string, lex_number, lex_radix_number, lex_word,
        lex_line_comment, lex_label, lex_operator,
    };

    TokenList out;
    int len = (int)strlen(source);
    int pos = 0;

    while (pos < len && out.size < MAX_TOKENS_PER_LINE) {
        if (std::isspace((unsigned char)source[pos])) { pos++; continue; }

        LexStep step = LexStep::NotMine;
        for (LexRule rule : RULES) {
            step = rule(source, len, pos, out);
            if (step != LexStep::NotMine) break;
        }

        if (step == LexStep::EndOfLine) break;
        if (step == LexStep::NotMine) pos++; // 知らない文字は読み飛ばす
    }

    if (out.size < MAX_TOKENS_PER_LINE) {
        push_token(out, TokenType::END_OF_FILE, "");
    } else {
        out.tokens[MAX_TOKENS_PER_LINE - 1] = {TokenType::END_OF_FILE, ""};
    }
    return out;
}

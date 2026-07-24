#pragma once

#define MAX_TOKEN_LEN 128
#define MAX_TOKENS_PER_LINE 128

enum class TokenType {
    NUMBER,
    IDENTIFIER,
    STRING,
    PRINT,
    LET,
    ASSIGN,
    PLUS,
    MINUS,
    MUL,
    DIV,
    POWER,
    LPAREN,
    RPAREN,
    GOTO,
    GOSUB,
    RETURN,
    IF,
    THEN,
    ELSE,
    ELSEIF,
    LABEL,   // *NAME 形式の行ラベル（定義・参照とも）
    REM,     // コメント（REM / ' 以降、行末まで）。本文を text に持つ
    FOR,
    TO,
    STEP,
    NEXT,
    NEW,
    LIST,
    RUN,
    READ,
    DATA,
    RESTORE,
    DIM,
    INPUT,
    END,
    STOP,
    INIT, CLEAR, NEWON, WIDTH, CONSOLE, CLS, LOCATE, REPEAT, UNTIL, AUTO,
    GET, FILES, SAVE, LOAD, KILL, NAME, AS, GPIO, ON, COLON,
    WINDOW, PSET, LINE, CIRCLE, POLY, PAINT, GET_AT, PUT_AT, COLOR,
    BRIGHTNESS,
    WAIT,
    BEEP, PLAY, MUSIC, SOUND,
    GT,
    LT,
    GTE,
    LTE,
    NEQ,
    COMMA,
    SEMICOLON,
    END_OF_FILE
};

struct Token {
    TokenType type;
    char text[MAX_TOKEN_LEN];
};

struct TokenList {
    Token tokens[MAX_TOKENS_PER_LINE];
    int size;

    TokenList() : size(0) {}
};

TokenList lex(const char* source);

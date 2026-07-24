#include <gtest/gtest.h>
#include "parser.h"
#include "lexer.h"
#include "mock_hal_display.h"
#include <string>

// AND/OR/NOT（ビット演算子） / DEF FN / 文字列配列

class OpDefFnTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
        mock_hal::reset();
    }
    std::string eval(const char* expr) {
        mock_hal::reset();
        std::string cmd = std::string("PRINT ") + expr;
        parse_and_execute(lex(cmd.c_str()));
        return mock_hal::get_raw_print_buffer();
    }
    void run(const char* line) { parse_and_execute(lex(line)); }
};

// --- AND / OR / NOT ---------------------------------------------------
TEST_F(OpDefFnTest, BitwiseAnd) {
    EXPECT_EQ(eval("6 AND 3"), "2\n");
    EXPECT_EQ(eval("255 AND 15"), "15\n");
    EXPECT_EQ(eval("12 AND 10"), "8\n");
}

TEST_F(OpDefFnTest, BitwiseOr) {
    EXPECT_EQ(eval("5 OR 2"), "7\n");
    EXPECT_EQ(eval("7 OR 8"), "15\n");
}

TEST_F(OpDefFnTest, BitwiseNot) {
    EXPECT_EQ(eval("NOT 0"), "-1\n");
    EXPECT_EQ(eval("NOT 5"), "-6\n");
}

TEST_F(OpDefFnTest, LogicalCombination) {
    EXPECT_EQ(eval("(3 > 1) AND (2 < 5)"), "1\n");
    EXPECT_EQ(eval("(3 > 1) OR (9 < 5)"), "1\n");
    EXPECT_EQ(eval("(3 < 1) AND (2 < 5)"), "0\n");
    EXPECT_EQ(eval("(3 < 1) OR (9 < 5)"), "0\n");
}

TEST_F(OpDefFnTest, OperatorPrecedence) {
    // AND は OR より強く、算術は AND より強い
    EXPECT_EQ(eval("5 AND 3 OR 8"), "9\n");   // (5&3)|8 = 1|8
    EXPECT_EQ(eval("2 + 1 AND 3"), "3\n");    // (2+1)&3
    EXPECT_EQ(eval("1 OR 0 AND 0"), "1\n");   // 1|(0&0) = 1
}

TEST_F(OpDefFnTest, AndOrInIfCondition) {
    store_line(10, lex("A=3 : B=7"));
    store_line(20, lex("IF A=3 AND B=7 THEN PRINT \"BOTH\""));
    store_line(30, lex("IF A=1 OR B=7 THEN PRINT \"EITHER\""));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "BOTH\nEITHER\n");
}

TEST_F(OpDefFnTest, AndOnStringErrors) {
    mock_hal::reset();
    parse_and_execute(lex("PRINT \"A\" AND 1"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("Type Mismatch"), std::string::npos);
}

// --- DEF FN -----------------------------------------------------------
TEST_F(OpDefFnTest, DefFnSquare) {
    store_line(10, lex("DEF FNSQ(X) = X * X"));
    store_line(20, lex("PRINT FNSQ(3)"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "9\n");
}

TEST_F(OpDefFnTest, DefFnExpression) {
    store_line(10, lex("DEF FNC(F) = (F - 32) * 5 / 9"));
    store_line(20, lex("PRINT FNC(212)"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "100\n");
}

TEST_F(OpDefFnTest, DefFnParameterScope) {
    // 仮引数と同名の変数が呼び出しで壊れない
    store_line(10, lex("X = 100"));
    store_line(20, lex("DEF FNADD(X) = X + 1"));
    store_line(30, lex("PRINT FNADD(5)"));
    store_line(40, lex("PRINT X"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "6\n100\n");
}

TEST_F(OpDefFnTest, DefFnNested) {
    store_line(10, lex("DEF FND(N) = N * 2"));
    store_line(20, lex("DEF FNT(N) = FND(N) + 1"));
    store_line(30, lex("PRINT FNT(10)"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "21\n");
}

TEST_F(OpDefFnTest, DefFnInLoop) {
    store_line(10, lex("DEF FNID(N) = N"));
    store_line(20, lex("FOR I=1 TO 3 : PRINT FNID(I) : NEXT I"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "1\n2\n3\n");
}

TEST_F(OpDefFnTest, UndefinedFnErrors) {
    mock_hal::reset();
    parse_and_execute(lex("PRINT FNNOPE(1)"));
    // FN で始まるが未定義 → 予約名なので明確なエラー
    std::string out = mock_hal::get_raw_print_buffer();
    EXPECT_NE(out.find("Undefined function"), std::string::npos) << out;
}

// --- 文字列配列 -------------------------------------------------------
TEST_F(OpDefFnTest, StringArray1D) {
    store_line(10, lex("DIM N$(3)"));
    store_line(20, lex("N$(0) = \"ALICE\" : N$(1) = \"BOB\""));
    store_line(30, lex("PRINT N$(0) : PRINT N$(1)"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "ALICE\nBOB\n");
}

TEST_F(OpDefFnTest, StringArrayUninitialisedIsEmpty) {
    store_line(10, lex("DIM N$(3)"));
    store_line(20, lex("PRINT \"[\"; N$(2); \"]\""));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "[]\n");
}

TEST_F(OpDefFnTest, StringArray2D) {
    store_line(10, lex("DIM G$(2,2)"));
    store_line(20, lex("G$(0,0) = \"X\" : G$(1,1) = \"O\""));
    store_line(30, lex("PRINT G$(0,0); G$(1,1)"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "XO\n");
}

TEST_F(OpDefFnTest, StringArrayReassign) {
    store_line(10, lex("DIM W$(2)"));
    store_line(20, lex("W$(0) = \"HI\" : W$(0) = \"BYE\""));
    store_line(30, lex("PRINT W$(0)"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "BYE\n");
}

// --- POKE / PEEK（論理メモリの読み書き） ------------------------------
TEST_F(OpDefFnTest, PokePeekRoundTrip) {
    store_line(10, lex("POKE 40000, 65"));
    store_line(20, lex("PRINT PEEK(40000)"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "65\n");
}

TEST_F(OpDefFnTest, PokeMasksToByte) {
    store_line(10, lex("POKE 40000, 300"));   // 300 & 0xFF = 44
    store_line(20, lex("PRINT PEEK(40000)"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "44\n");
}

TEST_F(OpDefFnTest, PokePeekLoop) {
    store_line(10, lex("FOR A=50000 TO 50003 : POKE A, A-50000 : NEXT A"));
    store_line(20, lex("FOR A=50000 TO 50003 : PRINT PEEK(A); : NEXT A"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "0123"); // 末尾 ; で改行なし
}

TEST_F(OpDefFnTest, PeekOutOfRangeErrors) {
    mock_hal::reset();
    parse_and_execute(lex("PRINT PEEK(70000)"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("out of range"), std::string::npos);
}

TEST_F(OpDefFnTest, PokeOutOfRangeErrors) {
    mock_hal::reset();
    parse_and_execute(lex("POKE -1, 5"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("out of range"), std::string::npos);
}

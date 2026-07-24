#include <gtest/gtest.h>
#include "parser.h"
#include "parser_internal.h"
#include "lexer.h"
#include "mock_hal_display.h"

// 7.2 低優先項目: ELSEIF / 行ラベル / INIT・NEWON 空実装

class ElseIfLabelTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_hal::reset();
        parse_and_execute(lex("NEW"));
    }
    void run(const char* line) { parse_and_execute(lex(line)); }
};

// --- ELSEIF ---------------------------------------------------------

TEST_F(ElseIfLabelTest, ElseIfFirstBranch) {
    run("X = 1 : IF X = 1 THEN PRINT \"A\" ELSEIF X = 2 THEN PRINT \"B\" ELSE PRINT \"C\"");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "A\n");
}

TEST_F(ElseIfLabelTest, ElseIfMiddleBranch) {
    run("X = 2 : IF X = 1 THEN PRINT \"A\" ELSEIF X = 2 THEN PRINT \"B\" ELSE PRINT \"C\"");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "B\n");
}

TEST_F(ElseIfLabelTest, ElseIfElseBranch) {
    run("X = 9 : IF X = 1 THEN PRINT \"A\" ELSEIF X = 2 THEN PRINT \"B\" ELSE PRINT \"C\"");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "C\n");
}

TEST_F(ElseIfLabelTest, ElseIfChainMultiple) {
    run("X = 3 : IF X = 1 THEN PRINT 1 ELSEIF X = 2 THEN PRINT 2 ELSEIF X = 3 THEN PRINT 3 ELSE PRINT 9");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "3\n");
}

TEST_F(ElseIfLabelTest, ElseIfNoMatchNoElse) {
    // どの条件にも合致せず ELSE も無ければ何も起きない
    run("X = 5 : IF X = 1 THEN PRINT 1 ELSEIF X = 2 THEN PRINT 2");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "");
}

// 既存の IF/ELSE 挙動が壊れていないこと
TEST_F(ElseIfLabelTest, PlainIfElseStillWorks) {
    run("IF 1 THEN PRINT \"T\" ELSE PRINT \"F\"");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "T\n");
    mock_hal::reset();
    run("IF 0 THEN PRINT \"T\" ELSE PRINT \"F\"");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "F\n");
}

// --- 行ラベル -------------------------------------------------------

TEST_F(ElseIfLabelTest, GotoLabel) {
    store_line(10, lex("GOTO *SKIP"));
    store_line(20, lex("PRINT \"NG\""));
    store_line(30, lex("*SKIP"));
    store_line(40, lex("PRINT \"OK\""));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "OK\n");
}

TEST_F(ElseIfLabelTest, GosubLabel) {
    store_line(10, lex("GOSUB *SUB"));
    store_line(20, lex("PRINT \"MAIN\" : END"));
    store_line(30, lex("*SUB"));
    store_line(40, lex("PRINT \"SUB\" : RETURN"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "SUB\nMAIN\n");
}

TEST_F(ElseIfLabelTest, LabelOnSameLineAsCode) {
    // `*NAME : 文` のように、ラベル定義と同じ行に文を続けられる
    store_line(10, lex("GOTO *LOOP"));
    store_line(20, lex("*LOOP : PRINT \"HERE\""));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "HERE\n");
}

TEST_F(ElseIfLabelTest, IfThenLabelGoto) {
    // Hu-BASIC 記法 `IF cond THEN *LABEL`（暗黙 GOTO）
    store_line(10, lex("X = 1"));
    store_line(20, lex("IF X = 1 THEN *DONE"));
    store_line(30, lex("PRINT \"NG\""));
    store_line(40, lex("*DONE"));
    store_line(50, lex("PRINT \"YES\""));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "YES\n");
}

TEST_F(ElseIfLabelTest, LabelAfterLineNumberNotMultiply) {
    // `40 *A` の 40 は行番号。直後の *A を乗算と誤認せずラベルとして扱う
    store_line(10, lex("GOTO *T"));
    store_line(20, lex("PRINT \"NG\""));
    store_line(40, lex("*T"));
    store_line(50, lex("PRINT \"OK\""));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "OK\n");
}

TEST_F(ElseIfLabelTest, MultiplyAfterNumberInExpr) {
    // 行番号でない NUMBER の直後の * は乗算のまま
    run("10 A = 5 * 3");
    run("20 PRINT A");
    mock_hal::reset();
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "15\n");
}

// --- THEN 省略（IF cond GOTO/GOSUB） -------------------------------

TEST_F(ElseIfLabelTest, IfGotoWithoutThen) {
    store_line(10, lex("A = 3"));
    store_line(20, lex("IF A = 3 GOTO 100"));
    store_line(30, lex("PRINT \"NG\""));
    store_line(100, lex("PRINT \"YES\""));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "YES\n");
}

TEST_F(ElseIfLabelTest, IfGotoWithoutThenFallsThrough) {
    store_line(10, lex("A = 5"));
    store_line(20, lex("IF A = 3 GOTO 100"));
    store_line(30, lex("PRINT \"OK\" : END"));
    store_line(100, lex("PRINT \"NG\""));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "OK\n");
}

TEST_F(ElseIfLabelTest, IfGosubWithoutThen) {
    store_line(10, lex("A = 1"));
    store_line(20, lex("IF A = 1 GOSUB 200"));
    store_line(30, lex("PRINT \"BACK\" : END"));
    store_line(200, lex("PRINT \"SUB\" : RETURN"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "SUB\nBACK\n");
}

TEST_F(ElseIfLabelTest, IfGotoLabelWithoutThen) {
    store_line(10, lex("A = 3"));
    store_line(20, lex("IF A = 3 GOTO *DONE"));
    store_line(30, lex("PRINT \"NG\""));
    store_line(40, lex("*DONE"));
    store_line(50, lex("PRINT \"YES\""));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "YES\n");
}

TEST_F(ElseIfLabelTest, IfMissingThenStillErrorsForStatement) {
    // GOTO/GOSUB 以外を THEN 無しで続けるのは従来どおりエラー
    parse_and_execute(lex("IF 1 PRINT 5"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("Missing THEN"), std::string::npos);
}

TEST_F(ElseIfLabelTest, MultiplyStillWorks) {
    // `A*B` は乗算のまま（ラベルと誤認しない）
    run("A = 6 : B = 7 : PRINT A*B");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "42\n");
}

TEST_F(ElseIfLabelTest, MultiplyNoSpace) {
    run("PRINT 3*4");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "12\n");
}

TEST_F(ElseIfLabelTest, OnGotoLabel) {
    store_line(10, lex("X = 2 : ON X GOTO *A, *B"));
    store_line(20, lex("*A"));
    store_line(30, lex("PRINT \"A\" : END"));
    store_line(40, lex("*B"));
    store_line(50, lex("PRINT \"B\" : END"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "B\n");
}

TEST_F(ElseIfLabelTest, UndefinedLabelErrors) {
    store_line(10, lex("GOTO *NOPE"));
    run("RUN");
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("Undefined label"), std::string::npos);
}

TEST_F(ElseIfLabelTest, LabelSurvivesListRoundTrip) {
    // 保存→detokenize 後もラベルが `*NAME` として復元される
    store_line(10, lex("*START"));
    mock_hal::reset();
    run("LIST");
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("*START"), std::string::npos);
}

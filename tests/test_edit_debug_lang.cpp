#include <gtest/gtest.h>
#include "parser.h"
#include "parser_internal.h"
#include "lexer.h"
#include "mock_hal_display.h"
#include <string>

// 編集系（RENUM / LIST 範囲 / DELETE）・デバッグ系（CONT / TRON）・
// 言語系（WHILE/WEND / PRINT USING / ON ERROR）

class EditDebugLangTest : public ::testing::Test {
protected:
    void SetUp() override {
        parse_and_execute(lex("NEW"));
        trace_enabled = false;
        mock_hal::reset();
    }
    void run(const char* line) { parse_and_execute(lex(line)); }
    std::string out() { return mock_hal::get_raw_print_buffer(); }
};

// --- LIST 範囲 --------------------------------------------------------
TEST_F(EditDebugLangTest, ListRange) {
    store_line(10, lex("PRINT 1"));
    store_line(20, lex("PRINT 2"));
    store_line(30, lex("PRINT 3"));
    mock_hal::reset();
    run("LIST 20-30");
    EXPECT_EQ(out().find("10 PRINT"), std::string::npos);
    EXPECT_NE(out().find("20 PRINT"), std::string::npos);
    EXPECT_NE(out().find("30 PRINT"), std::string::npos);
    mock_hal::reset();
    run("LIST 20");
    EXPECT_NE(out().find("20 PRINT"), std::string::npos);
    EXPECT_EQ(out().find("30 PRINT"), std::string::npos);
}

// --- DELETE -----------------------------------------------------------
TEST_F(EditDebugLangTest, DeleteRange) {
    store_line(10, lex("PRINT 1"));
    store_line(20, lex("PRINT 2"));
    store_line(30, lex("PRINT 3"));
    run("DELETE 15-25");
    mock_hal::reset();
    run("LIST");
    EXPECT_NE(out().find("10 PRINT"), std::string::npos);
    EXPECT_EQ(out().find("20 PRINT"), std::string::npos);
    EXPECT_NE(out().find("30 PRINT"), std::string::npos);
}

// --- RENUM ------------------------------------------------------------
TEST_F(EditDebugLangTest, RenumRewritesTargets) {
    store_line(5,  lex("GOTO 30"));
    store_line(10, lex("IF A=1 THEN 40 ELSE 30"));
    store_line(30, lex("ON X GOSUB 5, 10, 40"));
    store_line(40, lex("GOSUB 5"));
    run("RENUM 100, 10");
    mock_hal::reset();
    run("LIST");
    std::string s = out();
    EXPECT_NE(s.find("100 GOTO 120"), std::string::npos) << s;
    EXPECT_NE(s.find("110 IF A = 1 THEN 130 ELSE 120"), std::string::npos) << s;
    EXPECT_NE(s.find("120 ON X GOSUB 100 , 110 , 130"), std::string::npos) << s;
    EXPECT_NE(s.find("130 GOSUB 100"), std::string::npos) << s;
}

TEST_F(EditDebugLangTest, RenumProgramStillRuns) {
    store_line(10, lex("GOSUB 30"));
    store_line(20, lex("PRINT \"MAIN\" : END"));
    store_line(30, lex("PRINT \"SUB\" : RETURN"));
    run("RENUM 100, 5");
    mock_hal::reset();
    run("RUN");
    EXPECT_EQ(out(), "SUB\nMAIN\n");
}

// --- CONT / STOP ------------------------------------------------------
TEST_F(EditDebugLangTest, ContAfterStop) {
    store_line(10, lex("PRINT \"A\""));
    store_line(20, lex("STOP"));
    store_line(30, lex("PRINT \"B\""));
    run("RUN");
    EXPECT_NE(out().find("A\n"), std::string::npos);
    EXPECT_NE(out().find("Break in 20"), std::string::npos);
    mock_hal::reset();
    run("CONT");
    EXPECT_EQ(out(), "B\n");
}

TEST_F(EditDebugLangTest, ContResumesSameLine) {
    store_line(10, lex("STOP : PRINT \"AFTER\""));
    run("RUN");
    mock_hal::reset();
    run("CONT");
    EXPECT_EQ(out(), "AFTER\n"); // STOP の直後の文から再開する
}

TEST_F(EditDebugLangTest, CantContinueCases) {
    mock_hal::reset();
    run("CONT"); // 何も実行していない
    EXPECT_NE(out().find("Can't continue"), std::string::npos);

    store_line(10, lex("STOP"));
    store_line(20, lex("PRINT 1"));
    run("RUN");
    store_line(15, lex("REM edit")); // 編集すると再開不可
    mock_hal::reset();
    run("CONT");
    EXPECT_NE(out().find("Can't continue"), std::string::npos);
}

// --- TRON / TROFF -----------------------------------------------------
TEST_F(EditDebugLangTest, TraceOutput) {
    store_line(10, lex("A=1"));
    store_line(20, lex("PRINT A"));
    run("TRON");
    mock_hal::reset();
    run("RUN");
    EXPECT_NE(out().find("[10][20]"), std::string::npos) << out();
    run("TROFF");
    mock_hal::reset();
    run("RUN");
    EXPECT_EQ(out().find("[10]"), std::string::npos);
}

// --- WHILE / WEND -----------------------------------------------------
TEST_F(EditDebugLangTest, WhileLoop) {
    store_line(10, lex("I=0"));
    store_line(20, lex("WHILE I<3"));
    store_line(30, lex("I=I+1 : PRINT I"));
    store_line(40, lex("WEND"));
    store_line(50, lex("PRINT \"done\""));
    run("RUN");
    EXPECT_EQ(out(), "1\n2\n3\ndone\n");
}

TEST_F(EditDebugLangTest, WhileFalseSkipsBody) {
    store_line(10, lex("WHILE 0"));
    store_line(20, lex("PRINT \"never\""));
    store_line(30, lex("WEND"));
    store_line(40, lex("PRINT \"done\""));
    run("RUN");
    EXPECT_EQ(out(), "done\n");
}

TEST_F(EditDebugLangTest, WhileNested) {
    store_line(10, lex("A=0"));
    store_line(20, lex("WHILE A<2"));
    store_line(30, lex("A=A+1 : B=0"));
    store_line(40, lex("WHILE B<2"));
    store_line(50, lex("B=B+1 : PRINT A*10+B"));
    store_line(60, lex("WEND"));
    store_line(70, lex("WEND"));
    run("RUN");
    EXPECT_EQ(out(), "11\n12\n21\n22\n");
}

TEST_F(EditDebugLangTest, WhileSingleLine) {
    store_line(10, lex("I=0"));
    store_line(20, lex("WHILE I<3 : I=I+1 : PRINT I : WEND"));
    run("RUN");
    EXPECT_EQ(out(), "1\n2\n3\n");
}

TEST_F(EditDebugLangTest, WendWithoutWhileErrors) {
    store_line(10, lex("WEND"));
    run("RUN");
    EXPECT_NE(out().find("WEND without WHILE"), std::string::npos);
}

// --- PRINT USING ------------------------------------------------------
TEST_F(EditDebugLangTest, PrintUsingNumeric) {
    run("PRINT USING \"###.##\"; 3.14159");
    EXPECT_EQ(out(), "  3.14\n");
}

TEST_F(EditDebugLangTest, PrintUsingLiteralsAndMulti) {
    run("PRINT USING \"# + # = #\"; 1; 2; 3");
    EXPECT_EQ(out(), "1 + 2 = 3\n");
}

TEST_F(EditDebugLangTest, PrintUsingStringFields) {
    run("PRINT USING \"[&] [!]\"; \"ABC\"; \"XYZ\"");
    EXPECT_EQ(out(), "[ABC] [X]\n");
}

TEST_F(EditDebugLangTest, PrintUsingOverflowMarker) {
    run("PRINT USING \"###\"; 12345");
    EXPECT_NE(out().find("%"), std::string::npos);
}

TEST_F(EditDebugLangTest, PrintUsingFormatRepeats) {
    run("PRINT USING \"##:\"; 1; 2");
    EXPECT_EQ(out(), " 1: 2:\n");
}

// --- ON ERROR / RESUME / ERR / ERL -----------------------------------
TEST_F(EditDebugLangTest, OnErrorCatchesAndResumeNext) {
    store_line(10, lex("ON ERROR GOTO 100"));
    store_line(20, lex("X = 5 / 0"));
    store_line(30, lex("PRINT \"next\" : END"));
    store_line(100, lex("PRINT \"ERR=\"; ERR; \" ERL=\"; ERL"));
    store_line(110, lex("RESUME NEXT"));
    run("RUN");
    EXPECT_EQ(out(), "ERR=11 ERL=20\nnext\n");
}

TEST_F(EditDebugLangTest, ResumeToLine) {
    store_line(10, lex("ON ERROR GOTO 100"));
    store_line(20, lex("GOTO 9999"));
    store_line(30, lex("PRINT \"no\""));
    store_line(40, lex("PRINT \"recovered\" : END"));
    store_line(100, lex("RESUME 40"));
    run("RUN");
    EXPECT_EQ(out(), "recovered\n");
}

TEST_F(EditDebugLangTest, ErrorInsideHandlerReported) {
    store_line(10, lex("ON ERROR GOTO 100"));
    store_line(20, lex("X = 5 / 0"));
    store_line(100, lex("Y = 1 / 0"));
    run("RUN");
    EXPECT_NE(out().find("Error"), std::string::npos);
    EXPECT_NE(out().find("line 100"), std::string::npos);
}

TEST_F(EditDebugLangTest, OnErrorGotoZeroDisables) {
    store_line(10, lex("ON ERROR GOTO 100"));
    store_line(20, lex("ON ERROR GOTO 0"));
    store_line(30, lex("X = 5 / 0"));
    store_line(100, lex("PRINT \"handler\" : END"));
    run("RUN");
    EXPECT_EQ(out().find("handler"), std::string::npos);
    EXPECT_NE(out().find("Division by zero"), std::string::npos);
}

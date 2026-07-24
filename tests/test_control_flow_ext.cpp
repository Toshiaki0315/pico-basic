#include <gtest/gtest.h>
#include "parser.h"
#include "lexer.h"
#include "hal_display.h"
#include "mock_hal_display.h"
#include <string>

// §7.2: REPEAT/UNTIL, GET, CONSOLE/WIDTH

class ControlFlowExtTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
        mock_hal::reset();
    }
};

// ---------------------------------------------------------
// REPEAT ... UNTIL（後判定ループ）
// ---------------------------------------------------------

TEST_F(ControlFlowExtTest, RepeatUntilCountsUp) {
    store_line(10, lex("I = 0"));
    store_line(20, lex("REPEAT"));
    store_line(30, lex("I = I + 1"));
    store_line(40, lex("PRINT I"));
    store_line(50, lex("UNTIL I >= 3"));
    run_program();

    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "1\n2\n3\n");
}

TEST_F(ControlFlowExtTest, RepeatBodyRunsAtLeastOnce) {
    // 後判定なので、条件が最初から真でも 1 回は実行される
    store_line(10, lex("REPEAT"));
    store_line(20, lex("PRINT 42"));
    store_line(30, lex("UNTIL 1"));
    run_program();

    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "42\n");
}

TEST_F(ControlFlowExtTest, NestedRepeatUntil) {
    store_line(10, lex("A = 0"));
    store_line(20, lex("REPEAT"));
    store_line(30, lex("A = A + 1"));
    store_line(40, lex("B = 0"));
    store_line(50, lex("REPEAT"));
    store_line(60, lex("B = B + 1"));
    store_line(70, lex("UNTIL B >= 2"));
    store_line(80, lex("UNTIL A >= 2"));
    store_line(90, lex("PRINT A * 10 + B"));
    run_program();

    // A=2, B=2 で終わる
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "22\n");
}

TEST_F(ControlFlowExtTest, UntilWithoutRepeatIsError) {
    parse_and_execute(lex("UNTIL 1"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("UNTIL without REPEAT"), std::string::npos);
}

// ---------------------------------------------------------
// GET（ノンウェイト 1 文字入力）
// ---------------------------------------------------------

TEST_F(ControlFlowExtTest, GetReadsPressedKeyIntoStringVar) {
    hal_display_set_mock_input("A");
    parse_and_execute(lex("GET K$"));
    parse_and_execute(lex("PRINT K$"));

    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "A\n");
}

TEST_F(ControlFlowExtTest, GetReturnsEmptyWhenNoKey) {
    hal_display_set_mock_input(""); // 未入力
    parse_and_execute(lex("GET K$"));
    parse_and_execute(lex("PRINT LEN(K$)"));

    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "0\n");
}

TEST_F(ControlFlowExtTest, GetIntoNumericVarGivesCharCode) {
    hal_display_set_mock_input("A"); // 'A' = 65
    parse_and_execute(lex("GET K"));
    parse_and_execute(lex("PRINT K"));

    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "65\n");
}

// ---------------------------------------------------------
// CONSOLE / WIDTH
// ---------------------------------------------------------

TEST_F(ControlFlowExtTest, ConsoleSetsScrollRegion) {
    parse_and_execute(lex("CONSOLE 5, 10"));
    EXPECT_EQ(mock_hal::get_scroll_top(), 5);
    EXPECT_EQ(mock_hal::get_scroll_bottom(), 14); // 5 + 10 - 1
}

TEST_F(ControlFlowExtTest, ConsoleMovesCursorIntoRegion) {
    parse_and_execute(lex("CONSOLE 10, 5"));
    // カーソルが領域の先頭に移動していないと、領域外に文字が出続けて
    // CONSOLE が効かないように見える
    EXPECT_EQ(mock_hal::get_cursor_y(), 10);
    EXPECT_EQ(mock_hal::get_cursor_x(), 0);
}

TEST_F(ControlFlowExtTest, ConsoleWithoutArgsRestoresFullScreen) {
    parse_and_execute(lex("CONSOLE 5, 10"));
    parse_and_execute(lex("CONSOLE"));
    EXPECT_EQ(mock_hal::get_scroll_top(), 0);
    EXPECT_EQ(mock_hal::get_scroll_bottom(), 29);
}

TEST_F(ControlFlowExtTest, ConsoleRejectsBadRange) {
    parse_and_execute(lex("CONSOLE 40, 5")); // 開始が画面外
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("CONSOLE range"), std::string::npos);
}

TEST_F(ControlFlowExtTest, WidthAcceptsNativeSize) {
    parse_and_execute(lex("WIDTH 40, 30"));
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().empty())
        << mock_hal::get_raw_print_buffer();
}

TEST_F(ControlFlowExtTest, WidthRejectsUnsupportedSize) {
    parse_and_execute(lex("WIDTH 80"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("WIDTH only supports"), std::string::npos);
}

// ---------------------------------------------------------
// エラーコード表示（Hu-BASIC のコード番号を併記）
// ---------------------------------------------------------

TEST_F(ControlFlowExtTest, SyntaxErrorHasCode2) {
    store_line(10, lex("PRINT )"));
    run_program(5);
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("Error 2 in line 10"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

TEST_F(ControlFlowExtTest, TypeMismatchHasCode13) {
    store_line(10, lex("A$ = \"X\""));
    store_line(20, lex("GOTO A$"));
    run_program(5);
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("Error 13 in line 20"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

TEST_F(ControlFlowExtTest, UndefinedLineHasCode8) {
    store_line(10, lex("GOTO 999"));
    run_program(5);
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("Error 8 in line 999"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

TEST_F(ControlFlowExtTest, DuplicateDimHasCode10) {
    parse_and_execute(lex("DIM A(10)"));
    mock_hal::reset();
    parse_and_execute(lex("DIM A(10)")); // ダイレクトモード
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("Error 10:"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

TEST_F(ControlFlowExtTest, UncodedErrorKeepsPlainMessage) {
    // コード表に無いエラーは番号を付けず本文だけ出す
    store_line(10, lex("RETURN"));
    run_program(5);
    std::string out = mock_hal::get_raw_print_buffer();
    EXPECT_NE(out.find("Error in line 10: RETURN WITHOUT GOSUB"), std::string::npos) << out;
    EXPECT_EQ(out.find("Error 0"), std::string::npos) << "コード 0 を表示してしまっている";
}

// ---------------------------------------------------------
// AUTO（行番号自動生成）— コマンド解析と保留状態
// 実際のプロンプト表示は repl（Pico 対話ループ）側なのでここでは
// 開始番号・刻みが正しく解釈され、保留フラグが立つことだけを検証する
// ---------------------------------------------------------
TEST_F(ControlFlowExtTest, AutoDefaults) {
    parse_and_execute(lex("AUTO"));
    int start = -1, step = -1;
    EXPECT_TRUE(auto_mode_requested(&start, &step));
    EXPECT_EQ(start, 10);
    EXPECT_EQ(step, 10);
    // 消費後はフラグが下りる
    EXPECT_FALSE(auto_mode_requested(&start, &step));
}

TEST_F(ControlFlowExtTest, AutoStartOnly) {
    parse_and_execute(lex("AUTO 100"));
    int start = -1, step = -1;
    EXPECT_TRUE(auto_mode_requested(&start, &step));
    EXPECT_EQ(start, 100);
    EXPECT_EQ(step, 10);
}

TEST_F(ControlFlowExtTest, AutoStartAndStep) {
    parse_and_execute(lex("AUTO 100, 5"));
    int start = -1, step = -1;
    EXPECT_TRUE(auto_mode_requested(&start, &step));
    EXPECT_EQ(start, 100);
    EXPECT_EQ(step, 5);
}

TEST_F(ControlFlowExtTest, AutoStepZeroFallsBack) {
    // 刻み 0 は無限ループになるので既定 10 に戻す
    parse_and_execute(lex("AUTO 0, 0"));
    int start = -1, step = -1;
    EXPECT_TRUE(auto_mode_requested(&start, &step));
    EXPECT_EQ(start, 0);
    EXPECT_EQ(step, 10);
}

TEST_F(ControlFlowExtTest, AutoNotRequestedWithoutCommand) {
    parse_and_execute(lex("PRINT 1"));
    int start = -1, step = -1;
    EXPECT_FALSE(auto_mode_requested(&start, &step));
}

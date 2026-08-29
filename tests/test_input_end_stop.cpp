#include <gtest/gtest.h>
#include "parser.h"
#include "hal_display.h"
#include "mock_hal_display.h"
#include "lexer.h"

class InputEndStopTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
    }
    void TearDown() override {
        clear_program();
    }
};

TEST_F(InputEndStopTest, ExecuteEnd) {
    parse_and_execute(lex("10 A = 1"));
    parse_and_execute(lex("20 END"));
    parse_and_execute(lex("30 A = 2"));
    run_program();
    
    testing::internal::CaptureStdout();
    parse_and_execute(lex("PRINT A"));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output, "1\n");
}

TEST_F(InputEndStopTest, ExecuteStop) {
    parse_and_execute(lex("10 A = 1"));
    parse_and_execute(lex("20 STOP"));
    parse_and_execute(lex("30 A = 2"));
    
    testing::internal::CaptureStdout();
    run_program();
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(output.find("Break in 20") != std::string::npos);
    
    testing::internal::CaptureStdout();
    parse_and_execute(lex("PRINT A"));
    std::string output2 = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output2, "1\n");
}

// 期待値の "Name HELLO\n" は プロンプト + 打った文字のエコー + 確定の改行。
// そのあとに PRINT の "HELLO\n" が続く。差し込んだ値をそのまま返すだけだった
// 以前のモックはエコーを出さなかったが、実機は打鍵をそのまま返すので、
// 本物の行エディタを通すようになったいまはこちらが正しい出力になる
TEST_F(InputEndStopTest, ExecuteInputString) {
    hal_display_set_mock_input("HELLO");
    testing::internal::CaptureStdout();
    parse_and_execute(lex("INPUT \"Name \"; A$"));
    parse_and_execute(lex("PRINT A$"));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output, "Name HELLO\nHELLO\n");
}

TEST_F(InputEndStopTest, ExecuteInputNumber) {
    hal_display_set_mock_input("999");
    testing::internal::CaptureStdout();
    parse_and_execute(lex("INPUT B"));
    parse_and_execute(lex("PRINT B"));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output, "? 999\n999\n");
}

// 行エディタが本当に通っていることの確認。以前のモックは差し込んだ値を
// そのまま返すだけだったので、ここから下の挙動はホストで一度も走っていなかった。

// バックスペースで直前の 1 文字が消え、端末には「戻して空白で消して戻す」が出る
TEST_F(InputEndStopTest, InputHonoursBackspace) {
    hal_display_set_mock_input("ABX\bC");
    testing::internal::CaptureStdout();
    parse_and_execute(lex("INPUT A$"));
    parse_and_execute(lex("PRINT A$"));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output, "? ABX\b \bC\nABC\n");
}

// 端末が UTF-8 で送ってくる半角カタカナは JIS X 0201 の 1 バイトに畳む。
// ｱ = U+FF71 -> 0xB1(177)。エコーはシリアル向けに UTF-8 へ戻して返す
TEST_F(InputEndStopTest, InputFoldsUtf8HalfWidthKana) {
    hal_display_set_mock_input("\xEF\xBD\xB1");
    testing::internal::CaptureStdout();
    parse_and_execute(lex("INPUT A$"));
    parse_and_execute(lex("PRINT ASC(A$)"));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output, "? \xEF\xBD\xB1\n177\n");
}

// 表示できない文字（漢字など）は 3 バイトまとめて捨てる。
// 2 バイトしか捨てないと 1 バイトずれて後続が別の字に化ける
TEST_F(InputEndStopTest, InputDropsUndisplayableMultibyteWhole) {
    hal_display_set_mock_input("A\xE6\xBC\xA2" "B"); // A 漢 B
    testing::internal::CaptureStdout();
    parse_and_execute(lex("INPUT A$"));
    parse_and_execute(lex("PRINT LEN(A$)"));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output, "? AB\n2\n");
}

// --- INPUT 待ちの Ctrl-C ---------------------------------------------------
//
// 実行ループの Ctrl-C チェックは INPUT で塞がっている間は回らない。
// 読み取る側が自分で中断させないと、INPUT で止まったプログラムを止める手段が
// 電源の長押ししか無くなる。

TEST_F(InputEndStopTest, CtrlCAtInputPromptBreaksTheProgram) {
    hal_display_set_mock_input("\x03"); // Ctrl-C
    parse_and_execute(lex("10 INPUT A$"));
    parse_and_execute(lex("20 PRINT \"AFTER\""));

    testing::internal::CaptureStdout();
    run_program();
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("Break in 10"), std::string::npos) << output;
    EXPECT_EQ(output.find("AFTER"), std::string::npos) << output;
}

// 中断したところから CONT で再開できること（実行ループ側の Ctrl-C と同じ扱い）
TEST_F(InputEndStopTest, CtrlCAtInputPromptAllowsCont) {
    hal_display_set_mock_input("\x03");
    parse_and_execute(lex("10 INPUT A$"));
    parse_and_execute(lex("20 PRINT \"AFTER\""));
    run_program();

    hal_display_set_mock_input("HI"); // 今度はちゃんと入力する
    testing::internal::CaptureStdout();
    parse_and_execute(lex("CONT"));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("AFTER"), std::string::npos) << output;
}

// ダイレクトモードの INPUT では止めるプログラムが無いので行番号を出さない
TEST_F(InputEndStopTest, CtrlCAtDirectModeInputSaysBreakWithoutLine) {
    hal_display_set_mock_input("\x03");
    testing::internal::CaptureStdout();
    parse_and_execute(lex("INPUT A$"));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("Break"), std::string::npos) << output;
    EXPECT_EQ(output.find("Break in"), std::string::npos) << output;
}

// 中断されたら変数には書かない
TEST_F(InputEndStopTest, CtrlCAtInputLeavesVariableUntouched) {
    parse_and_execute(lex("A$ = \"KEEP\""));
    hal_display_set_mock_input("\x03");
    parse_and_execute(lex("INPUT A$"));

    mock_hal::reset();
    parse_and_execute(lex("PRINT A$"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "KEEP\n");
}

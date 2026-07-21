#include <gtest/gtest.h>
#include "parser.h"
#include "lexer.h"
#include "mock_hal_display.h"
#include <iostream>
#include <string>

class SoundLogicTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
        mock_hal::reset();
    }
};

TEST_F(SoundLogicTest, BeepCommand) {
    EXPECT_NO_THROW(parse_and_execute(lex("BEEP")));
}

TEST_F(SoundLogicTest, SoundCommand) {
    EXPECT_NO_THROW(parse_and_execute(lex("SOUND 440, 500")));
    EXPECT_NO_THROW(parse_and_execute(lex("SOUND 880, 250")));
}

TEST_F(SoundLogicTest, MusicCommandBasic) {
    EXPECT_NO_THROW(parse_and_execute(lex("MUSIC \"CDE\"")));
    EXPECT_NO_THROW(parse_and_execute(lex("MUSIC \"O5 C D+ E- R4\"")));
}

TEST_F(SoundLogicTest, MusicCommandAdvanced) {
    // Check various MML parameters
    EXPECT_NO_THROW(parse_and_execute(lex("MUSIC \"T160 L8 V10 C D E F G A B > C < C\"")));
    EXPECT_NO_THROW(parse_and_execute(lex("MUSIC \"C. D8 E16\"")));
}

TEST_F(SoundLogicTest, MusicErrorHandling) {
    parse_and_execute(lex("MUSIC 123"));
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().find("Type Mismatch") != std::string::npos);
}

// ---------------------------------------------------------
// 3声（PSG 相当）の合成。発音内容は hal_sound の stdout 出力で確認する
// ---------------------------------------------------------

static std::string play_and_capture(const char* source) {
    testing::internal::CaptureStdout();
    parse_and_execute(lex(source));
    return testing::internal::GetCapturedStdout();
}

TEST_F(SoundLogicTest, PlayThreeVoicesSimultaneously) {
    std::string out = play_and_capture("PLAY \"C\",\"E\",\"G\"");

    // 3声が 1 回の発音にまとめられること（順番に鳴らすのではない）
    EXPECT_NE(out.find("3 voice(s)"), std::string::npos) << out;
    EXPECT_NE(out.find("ch0="), std::string::npos) << out;
    EXPECT_NE(out.find("ch1="), std::string::npos) << out;
    EXPECT_NE(out.find("ch2="), std::string::npos) << out;
}

TEST_F(SoundLogicTest, SingleVoiceStillPlaysOneNotePerStep) {
    std::string out = play_and_capture("PLAY \"CD\"");

    // 単音は従来どおり 1 音ずつ鳴る
    EXPECT_EQ(out.find("3 voice(s)"), std::string::npos) << out;
    EXPECT_NE(out.find("1 voice(s)"), std::string::npos) << out;
}

TEST_F(SoundLogicTest, VoicesWithDifferentNoteLengthsSplitIntoSteps) {
    // ch0 は 4分音符、ch1 は 8分音符 2つ。ch1 の切り替わりでステップが分割される
    std::string out = play_and_capture("MUSIC \"C4\",\"E8E8\"");

    // 2ステップとも 2声が鳴っており、ch0 は鳴りっぱなしになる
    size_t first = out.find("2 voice(s)");
    ASSERT_NE(first, std::string::npos) << out;
    EXPECT_NE(out.find("2 voice(s)", first + 1), std::string::npos) << out;
}

TEST_F(SoundLogicTest, VolumeIsPerVoice) {
    std::string out = play_and_capture("MUSIC \"V5C\",\"V15C\"");

    EXPECT_NE(out.find("ch0=261.63Hz(Vol:5)"), std::string::npos) << out;
    EXPECT_NE(out.find("ch1=261.63Hz(Vol:15)"), std::string::npos) << out;
}

TEST_F(SoundLogicTest, RepeatedNoteIsSeparatedByAGap) {
    // 同じ高さの音が連続しても、間に切れ目が入って 1 音に繋がらないこと
    std::string out = play_and_capture("PLAY \"CC\"");

    size_t first_note = out.find("ch0=261.63Hz");
    ASSERT_NE(first_note, std::string::npos) << out;
    size_t gap = out.find("REST", first_note);
    ASSERT_NE(gap, std::string::npos) << out;
    EXPECT_NE(out.find("ch0=261.63Hz", gap), std::string::npos) << out;
}

TEST_F(SoundLogicTest, SustainedVoiceIsNotRetriggeredByAnotherVoice) {
    // ch1 が切り替わっても ch0 の音は鳴り続ける（途切れて再発音されない）
    std::string out = play_and_capture("MUSIC \"C4\",\"E8E8\"");

    // ch1 の切れ目の間も ch0 だけが鳴っているステップが存在する
    EXPECT_NE(out.find("1 voice(s)"), std::string::npos) << out;
    EXPECT_EQ(out.find("REST", 0), out.rfind("REST")) << "曲の途中で全体が無音になっている: " << out;
}

TEST_F(SoundLogicTest, MoreThanThreeVoicesIsAnError) {
    parse_and_execute(lex("MUSIC \"C\",\"E\",\"G\",\"B\""));
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().find("Too many voices") != std::string::npos);
}

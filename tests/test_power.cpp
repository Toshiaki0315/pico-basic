#include <gtest/gtest.h>
#include "parser.h"
#include "lexer.h"
#include "hal_display.h"
#include "hal_battery.h"
#include "mock_hal_display.h"
#include <cstdio>

// 電源まわり: 電源ボタンの長押し判定と POWEROFF 文。
//
// 長押しの状態機械は hal_battery.h の power_key_step() に切り出してある。
// 実機の GPIO と時刻を渡すだけの薄い皮（hal_battery_power_key_held）は
// #if で実機側にしか無いので、ロジックだけをここで押さえる。

// ---------------------------------------------------------
// 長押し判定
// ---------------------------------------------------------

TEST(PowerKeyTest, DoesNotFireBeforeTheHoldTime) {
    PowerKeyState st;
    EXPECT_FALSE(power_key_step(st, true, 1000));                        // 押し始め
    EXPECT_FALSE(power_key_step(st, true, 1000 + POWER_KEY_HOLD_MS - 1)); // 1ms 手前
}

TEST(PowerKeyTest, FiresOnceTheHoldTimeIsReached) {
    PowerKeyState st;
    power_key_step(st, true, 1000);
    EXPECT_TRUE(power_key_step(st, true, 1000 + POWER_KEY_HOLD_MS));
}

// 押しっぱなしで何度もポーリングされても、知らせるのは 1 度だけ。
// そうでないと電源が切れなかったときに何度も切りに行ってしまう
TEST(PowerKeyTest, FiresOnlyOncePerHold) {
    PowerKeyState st;
    power_key_step(st, true, 0);
    EXPECT_TRUE(power_key_step(st, true, POWER_KEY_HOLD_MS));
    EXPECT_FALSE(power_key_step(st, true, POWER_KEY_HOLD_MS + 500));
    EXPECT_FALSE(power_key_step(st, true, POWER_KEY_HOLD_MS + 5000));
}

// 一度離せば、次の長押しはまた成立する
TEST(PowerKeyTest, RearmsAfterRelease) {
    PowerKeyState st;
    power_key_step(st, true, 0);
    EXPECT_TRUE(power_key_step(st, true, POWER_KEY_HOLD_MS));
    EXPECT_FALSE(power_key_step(st, false, POWER_KEY_HOLD_MS + 10)); // 離した
    power_key_step(st, true, POWER_KEY_HOLD_MS + 20);                // 押し直し
    EXPECT_FALSE(power_key_step(st, true, POWER_KEY_HOLD_MS + 20 + POWER_KEY_HOLD_MS - 1));
    EXPECT_TRUE(power_key_step(st, true, POWER_KEY_HOLD_MS + 20 + POWER_KEY_HOLD_MS));
}

// 途中で離したら測り直し。押した通算時間ではなく連続時間で判定する
TEST(PowerKeyTest, ReleasingRestartsTheTimer) {
    PowerKeyState st;
    power_key_step(st, true, 0);
    EXPECT_FALSE(power_key_step(st, true, POWER_KEY_HOLD_MS - 100));
    EXPECT_FALSE(power_key_step(st, false, POWER_KEY_HOLD_MS - 50));
    power_key_step(st, true, POWER_KEY_HOLD_MS);
    EXPECT_FALSE(power_key_step(st, true, POWER_KEY_HOLD_MS + 100)); // 通算では超えている
}

// 起動直後は now_ms が 0 になり得る。0 を「押していない」の目印に使うと
// その 1ms の間だけ時間の計測が始まらない
TEST(PowerKeyTest, WorksWhenTheHoldStartsAtTimeZero) {
    PowerKeyState st;
    EXPECT_FALSE(power_key_step(st, true, 0));
    EXPECT_TRUE(power_key_step(st, true, POWER_KEY_HOLD_MS));
}

// 押していないうちは何も起きない
TEST(PowerKeyTest, StaysQuietWhileTheKeyIsUp) {
    PowerKeyState st;
    for (uint32_t t = 0; t < POWER_KEY_HOLD_MS * 3; t += 50)
        EXPECT_FALSE(power_key_step(st, false, t)) << t;
}

// ---------------------------------------------------------
// POWEROFF 文
// ---------------------------------------------------------

class PowerTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
        mock_hal::reset();
    }
    void TearDown() override { clear_program(); }
};

TEST_F(PowerTest, PowerOffClosesOpenFilesFirst) {
    // 切る前に書き込みを確定させないとデータが失われる
    store_line(10, lex("OPEN \"PW.DAT\" FOR OUTPUT AS #1"));
    store_line(20, lex("PRINT #1, \"SAVED\""));
    store_line(30, lex("POWEROFF"));
    parse_and_execute(lex("RUN"));

    // 閉じられているので読み戻せる
    clear_program();
    mock_hal::reset();
    store_line(10, lex("OPEN \"PW.DAT\" FOR INPUT AS #1"));
    store_line(20, lex("LINE INPUT #1, A$"));
    store_line(30, lex("CLOSE #1"));
    store_line(40, lex("PRINT A$"));
    parse_and_execute(lex("RUN"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "SAVED\n");
}

TEST_F(PowerTest, PowerOffReportsWhenPowerStaysOn) {
    // ホストでは電源が切れないので、切れなかった旨まで出る
    mock_hal::reset();
    parse_and_execute(lex("POWEROFF"));
    std::string out = mock_hal::get_raw_print_buffer();
    EXPECT_NE(out.find("POWER OFF"), std::string::npos) << out;
    EXPECT_NE(out.find("STILL POWERED"), std::string::npos) << out;
}

TEST_F(PowerTest, PowerOffClearsDeferredDrawing) {
    // SYNC OFF のままだと「POWER OFF」の表示が出ないまま切れてしまう
    parse_and_execute(lex("SYNC OFF"));
    parse_and_execute(lex("POWEROFF"));
    EXPECT_FALSE(hal_display_is_deferred());
}

// 電源が切れなかったとき、閉じたファイルのまま実行が続くとデータが落ちる。
// ホストでは必ず「切れない」経路を通るので、そのまま回帰テストになる
TEST_F(PowerTest, PowerOffStopsTheProgramWhenPowerStaysOn) {
    store_line(10, lex("POWEROFF"));
    store_line(20, lex("PRINT \"AFTER\""));
    mock_hal::reset();
    parse_and_execute(lex("RUN"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer().find("AFTER"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

// 同じ行に続く文も実行しない（END と同じ扱い）
TEST_F(PowerTest, PowerOffStopsRestOfTheSameLine) {
    store_line(10, lex("POWEROFF : PRINT \"AFTER\""));
    mock_hal::reset();
    parse_and_execute(lex("RUN"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer().find("AFTER"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

// 停止しても、閉じた内容は確定していること（1 の対の確認）
TEST_F(PowerTest, PowerOffFlushesBeforeStopping) {
    store_line(10, lex("OPEN \"PW2.DAT\" FOR OUTPUT AS #1"));
    store_line(20, lex("PRINT #1, \"FLUSHED\""));
    store_line(30, lex("POWEROFF"));
    store_line(40, lex("PRINT #1, \"NEVER\""));
    parse_and_execute(lex("RUN"));

    clear_program();
    mock_hal::reset();
    store_line(10, lex("OPEN \"PW2.DAT\" FOR INPUT AS #1"));
    store_line(20, lex("LINE INPUT #1, A$"));
    store_line(30, lex("CLOSE #1"));
    store_line(40, lex("PRINT A$"));
    parse_and_execute(lex("RUN"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "FLUSHED\n");
}

TEST_F(PowerTest, PowerOffSurvivesListRoundTrip) {
    store_line(10, lex("POWEROFF"));
    mock_hal::reset();
    parse_and_execute(lex("LIST"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("POWEROFF"), std::string::npos);
}

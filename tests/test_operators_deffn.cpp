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

// --- MOD / \ / XOR / &H / 新関数（N-BASIC 互換バッチ） ----------------
TEST_F(OpDefFnTest, ModOperator) {
    EXPECT_EQ(eval("10 MOD 3"), "1\n");
    EXPECT_EQ(eval("8 MOD 4"), "0\n");
    EXPECT_EQ(eval("2 + 7 MOD 3"), "3\n");   // MOD は + より強い
}

TEST_F(OpDefFnTest, IntDivOperator) {
    EXPECT_EQ(eval("7 \\ 2"), "3\n");
    EXPECT_EQ(eval("10 \\ 3 \\ 2"), "1\n");  // 左結合
    EXPECT_EQ(eval("7 \\ 2 + 1"), "4\n");    // \ は + より強い
}

TEST_F(OpDefFnTest, XorOperator) {
    EXPECT_EQ(eval("6 XOR 3"), "5\n");
    EXPECT_EQ(eval("1 OR 2 XOR 2"), "1\n");  // XOR は OR より緩い
    EXPECT_EQ(eval("5 XOR 5"), "0\n");
}

TEST_F(OpDefFnTest, DivisionByZeroInModIntDiv) {
    mock_hal::reset();
    parse_and_execute(lex("PRINT 5 MOD 0"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("Division by zero"), std::string::npos);
    mock_hal::reset();
    parse_and_execute(lex("PRINT 5 \\ 0"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("Division by zero"), std::string::npos);
}

TEST_F(OpDefFnTest, HexBinLiterals) {
    EXPECT_EQ(eval("&HFF"), "255\n");
    EXPECT_EQ(eval("&H10"), "16\n");
    EXPECT_EQ(eval("&B1010"), "10\n");
    EXPECT_EQ(eval("&H3E"), "62\n");         // SOUND ミキサーの定番値
}

TEST_F(OpDefFnTest, HexLiteralSurvivesList) {
    store_line(10, lex("SOUND 7, &H3E"));
    mock_hal::reset();
    list_program();
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("&H3E"), std::string::npos);
}

TEST_F(OpDefFnTest, StringFunctions) {
    EXPECT_EQ(eval("HEX$(255)"), "FF\n");
    EXPECT_EQ(eval("STRING$(3, \"AB\")"), "AAA\n");  // 先頭文字を繰り返す
    EXPECT_EQ(eval("STRING$(2, 66)"), "BB\n");       // 文字コード指定
    EXPECT_EQ(eval("SPACE$(3) + \"X\""), "   X\n");
    EXPECT_EQ(eval("INSTR(\"HELLO\", \"LL\")"), "3\n");
    EXPECT_EQ(eval("INSTR(\"HELLO\", \"Z\")"), "0\n");
    EXPECT_EQ(eval("INSTR(4, \"AABBAABB\", \"AA\")"), "5\n"); // 開始位置指定
}

TEST_F(OpDefFnTest, TimerIsMonotonic) {
    // 値そのものは環境依存なので、数値であり負でないことだけ確認する
    EXPECT_EQ(eval("TIMER >= 0"), "1\n");
}

TEST_F(OpDefFnTest, PointReadsPixel) {
    mock_hal::reset(); // フレームバッファを消してから描く
    parse_and_execute(lex("WINDOW"));
    parse_and_execute(lex("PSET (5,5), 4"));
    parse_and_execute(lex("PRINT POINT(5,5); POINT(6,6); POINT(400,5)"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("40-1"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

// --- 半角カタカナ（JIS X 0201, 0xA1-0xDF）---------------------------
TEST_F(OpDefFnTest, KatakanaBytesPassThroughString) {
    // "スコア" = BD BA B1。文字列内のバイトがそのまま出力される
    mock_hal::reset();
    parse_and_execute(lex("PRINT \"\xBD\xBA\xB1\""));
    std::string out = mock_hal::get_raw_print_buffer();
    ASSERT_EQ(out.size(), 4u); // 3 バイト + 改行
    EXPECT_EQ((unsigned char)out[0], 0xBD);
    EXPECT_EQ((unsigned char)out[1], 0xBA);
    EXPECT_EQ((unsigned char)out[2], 0xB1);
}

TEST_F(OpDefFnTest, KatakanaViaChr) {
    // CHR$(&HB1) = ア。&HB1..&HB3 でアイウ
    mock_hal::reset();
    parse_and_execute(lex("PRINT CHR$(&HB1); CHR$(&HB2); CHR$(&HB3)"));
    std::string out = mock_hal::get_raw_print_buffer();
    ASSERT_GE(out.size(), 3u);
    EXPECT_EQ((unsigned char)out[0], 0xB1);
    EXPECT_EQ((unsigned char)out[1], 0xB2);
    EXPECT_EQ((unsigned char)out[2], 0xB3);
}

TEST_F(OpDefFnTest, KatakanaLenCountsBytes) {
    // 半角カタカナは 1 バイト = 1 文字
    EXPECT_EQ(eval("LEN(\"\xB1\xB2\xB3\")"), "3\n");
}

TEST_F(OpDefFnTest, KatakanaMidExtracts) {
    // MID$/ASC も 1 バイト単位で扱える
    mock_hal::reset();
    parse_and_execute(lex("PRINT ASC(MID$(\"\xB1\xB2\xB3\", 2, 1))"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "178\n"); // 0xB2 = 178
}

// --- BATTERY()（バッテリ電圧・残量）----------------------------------
#include "hal_battery.h"

TEST_F(OpDefFnTest, BatteryVoltageIsReported) {
    hal_battery_set_mock_millivolts(3850);
    EXPECT_EQ(eval("BATTERY(0)"), "3850\n");
}

TEST_F(OpDefFnTest, BatteryPercentMapsVoltage) {
    hal_battery_set_mock_millivolts(4200); // 満充電
    EXPECT_EQ(eval("BATTERY(1)"), "100\n");
    hal_battery_set_mock_millivolts(3300); // 空とみなす電圧
    EXPECT_EQ(eval("BATTERY(1)"), "0\n");
    hal_battery_set_mock_millivolts(3750); // ちょうど中間
    EXPECT_EQ(eval("BATTERY(1)"), "50\n");
}

TEST_F(OpDefFnTest, BatteryPercentIsClamped) {
    hal_battery_set_mock_millivolts(4500); // 満充電を超えても 100 まで
    EXPECT_EQ(eval("BATTERY(1)"), "100\n");
    hal_battery_set_mock_millivolts(2000); // 下限を割っても 0 止まり
    EXPECT_EQ(eval("BATTERY(1)"), "0\n");
}

TEST_F(OpDefFnTest, BatteryPresenceWithoutUsbIsCertain) {
    // USB が無いのに動いている = 電源は電池しかない
    hal_battery_set_mock_usb(0);
    hal_battery_set_mock_millivolts(3700);
    EXPECT_EQ(eval("BATTERY(2)"), "1\n");
}

TEST_F(OpDefFnTest, BatteryPresenceOnUsbBelowFloatIsCertain) {
    // USB 給電でも満充電未満なら、充電中の電池がぶら下がっている
    hal_battery_set_mock_usb(1);
    hal_battery_set_mock_millivolts(3700);
    EXPECT_EQ(eval("BATTERY(2)"), "1\n");
}

TEST_F(OpDefFnTest, BatteryPresenceOnUsbAtFloatIsUnknown) {
    // 実機で電池なし・USB 給電のとき 4196mV になった。満充電の電池と区別できない
    hal_battery_set_mock_usb(1);
    hal_battery_set_mock_millivolts(4196);
    EXPECT_EQ(eval("BATTERY(2)"), "2\n") << "判別できないのに 1 を返している";
}

TEST_F(OpDefFnTest, DeeplyDischargedCellIsStillPresent) {
    // 実機で空の電池を挿したとき 2340mV になった。電池が無ければ充電 IC が
    // 4.2V まで持ち上げるので、低電圧はむしろ「電池がある」証拠
    hal_battery_set_mock_usb(1);
    hal_battery_set_mock_millivolts(2340);
    EXPECT_EQ(eval("BATTERY(2)"), "1\n") << "空の電池を「無し」と誤判定している";
}

TEST_F(OpDefFnTest, PresenceNeverClaimsAbsence) {
    // この基板では電池が「無い」ことを証明できないので 0 は返さない
    hal_battery_set_mock_usb(1);
    for (int mv = 0; mv <= 4300; mv += 100) {
        hal_battery_set_mock_millivolts(mv);
        EXPECT_NE(battery_presence(mv, 1), 0) << mv;
    }
}

TEST_F(OpDefFnTest, BatteryUsbPowerFlag) {
    hal_battery_set_mock_usb(1);
    EXPECT_EQ(eval("BATTERY(3)"), "1\n");
    hal_battery_set_mock_usb(0);
    EXPECT_EQ(eval("BATTERY(3)"), "0\n");
}

TEST_F(OpDefFnTest, BatteryRejectsBadArgument) {
    mock_hal::reset();
    parse_and_execute(lex("PRINT BATTERY(9)"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("must be 0 to 3"), std::string::npos);
}

// --- BATTERY 文（引数なしの状態表示）---------------------------------
TEST_F(OpDefFnTest, BatteryStatementShowsSummary) {
    // 実機で電池なし・USB 給電のときの値
    hal_battery_set_mock_millivolts(4196);
    hal_battery_set_mock_usb(1);
    mock_hal::reset();
    parse_and_execute(lex("BATTERY"));
    std::string out = mock_hal::get_raw_print_buffer();
    EXPECT_NE(out.find("4.20V"), std::string::npos) << "切り捨てて 4.19V になっている: " << out;
    EXPECT_NE(out.find("99%"), std::string::npos) << out;
    EXPECT_NE(out.find("USB"), std::string::npos) << out;
    EXPECT_NE(out.find("UNKNOWN"), std::string::npos) << out;
}

TEST_F(OpDefFnTest, BatteryStatementOnBattery) {
    hal_battery_set_mock_millivolts(3650);
    hal_battery_set_mock_usb(0);
    mock_hal::reset();
    parse_and_execute(lex("BATTERY"));
    std::string out = mock_hal::get_raw_print_buffer();
    EXPECT_NE(out.find("3.65V"), std::string::npos) << out;
    EXPECT_NE(out.find("BATT"), std::string::npos) << out;
    EXPECT_NE(out.find("CELL OK"), std::string::npos) << out;
}

TEST_F(OpDefFnTest, BatteryStatementWorksInProgram) {
    hal_battery_set_mock_millivolts(3700);
    hal_battery_set_mock_usb(0);
    store_line(10, lex("BATTERY"));
    store_line(20, lex("PRINT \"AFTER\""));
    run("RUN");
    // 状態表示のあとで次の行も実行される（文として正しく終わっている）
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("AFTER"), std::string::npos);
}

TEST_F(OpDefFnTest, BatteryFunctionStillWorksAfterStatement) {
    // 引数つきは従来どおり関数として評価される
    hal_battery_set_mock_millivolts(3750);
    hal_battery_set_mock_usb(0);
    EXPECT_EQ(eval("BATTERY(0)"), "3750\n");
    EXPECT_EQ(eval("BATTERY(1)"), "50\n");
}

// --- PIN() / ADIN() / CPUTEMP（GPIO・アナログ入力・内蔵温度）-----------
#include "hal_adc.h"
#include "hal_gpio.h"

// eval() は先に mock_hal::reset() を呼んで GPIO の状態も消すため、
// ピンの値を仕込む系のテストは parse_and_execute を直接使う。
static std::string print_expr(const char* expr) {
    std::string cmd = std::string("PRINT ") + expr;
    parse_and_execute(lex(cmd.c_str()));
    return mock_hal::get_raw_print_buffer();
}

TEST_F(OpDefFnTest, PinReadsGpioLevel) {
    hal_gpio_write(5, true);
    EXPECT_EQ(print_expr("PIN(5)"), "1\n");
    mock_hal::reset();
    hal_gpio_write(5, false);
    EXPECT_EQ(print_expr("PIN(5)"), "0\n");
}

TEST_F(OpDefFnTest, PinRejectsOutOfRange) {
    // エラーは例外ではなく画面に出る（他の組み込み関数と同じ扱い）
    parse_and_execute(lex("PRINT PIN(30)"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("PIN argument must be"), std::string::npos);
    mock_hal::reset();
    parse_and_execute(lex("PRINT PIN(-1)"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("PIN argument must be"), std::string::npos);
}

TEST_F(OpDefFnTest, PinIsUsableAfterGpioInputMode) {
    // 入力に設定してから読む、という本来の使い方
    // mock_hal::reset() は GPIO の状態も消すので、仕込んだ後は呼ばない
    parse_and_execute(lex("GPIO 8, 0, 0, 1"));
    hal_gpio_write(8, true);
    EXPECT_NE(print_expr("PIN(8)").find("1"), std::string::npos);
}

TEST_F(OpDefFnTest, AdinReadsRawValue) {
    hal_adc_set_mock_raw(28, 2048);
    EXPECT_EQ(print_expr("ADIN(28)"), "2048\n");
    mock_hal::reset();
    hal_adc_set_mock_raw(29, 4095);
    EXPECT_EQ(print_expr("ADIN(29)"), "4095\n");
}

TEST_F(OpDefFnTest, AdinAllowsBatteryPin) {
    // GPIO27 は電池の分圧。読むだけなら無害なので許可している
    hal_adc_set_mock_raw(27, 1234);
    EXPECT_EQ(print_expr("ADIN(27)"), "1234\n");
}

TEST_F(OpDefFnTest, AdinRejectsBatEnPin) {
    // GPIO26 = BAT_EN。アナログ入力にすると電源が落ちうるので拒否する
    EXPECT_FALSE(adc_pin_allowed(26));
    parse_and_execute(lex("PRINT ADIN(26)"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("BAT_EN"), std::string::npos);
}

TEST_F(OpDefFnTest, AdinRejectsNonAdcPins) {
    parse_and_execute(lex("PRINT ADIN(5)"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("ADIN argument must be"), std::string::npos);
    mock_hal::reset();
    parse_and_execute(lex("PRINT ADIN(30)"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("ADIN argument must be"), std::string::npos);
}

TEST_F(OpDefFnTest, AdcPinAllowedCoversOnlyFreePins) {
    EXPECT_FALSE(adc_pin_allowed(25));
    EXPECT_TRUE(adc_pin_allowed(27));
    EXPECT_TRUE(adc_pin_allowed(28));
    EXPECT_TRUE(adc_pin_allowed(29));
    EXPECT_FALSE(adc_pin_allowed(30));
}

TEST_F(OpDefFnTest, CpuTempIsReadWithoutParens) {
    hal_adc_set_mock_temp_c(25.0f);
    EXPECT_EQ(print_expr("CPUTEMP"), "25\n");
}

TEST_F(OpDefFnTest, CpuTempIsUsableInExpression) {
    hal_adc_set_mock_temp_c(30.0f);
    EXPECT_EQ(print_expr("CPUTEMP > 20"), "1\n");
}

TEST_F(OpDefFnTest, PinAndAdinWorkInsideProgram) {
    hal_gpio_write(9, true);
    hal_adc_set_mock_raw(28, 100);
    store_line(10, lex("A = PIN(9)"));
    store_line(20, lex("B = ADIN(28)"));
    store_line(30, lex("PRINT A; B"));
    parse_and_execute(lex("RUN"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("1100"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

// --- 半角カタカナのシリアル出力（UTF-8 変換）---------------------------
#include "kana_utf8.h"

TEST_F(OpDefFnTest, KanaToUtf8MapsAiu) {
    char u[3];
    // 0xB1 = ｱ → U+FF71 → EF BD B1
    EXPECT_EQ(jis_kana_to_utf8(0xB1, u), 3);
    EXPECT_EQ((unsigned char)u[0], 0xEF);
    EXPECT_EQ((unsigned char)u[1], 0xBD);
    EXPECT_EQ((unsigned char)u[2], 0xB1);
}

TEST_F(OpDefFnTest, KanaToUtf8CoversBothEnds) {
    char u[3];
    // 0xA1 = ｡ → U+FF61 → EF BD A1
    EXPECT_EQ(jis_kana_to_utf8(0xA1, u), 3);
    EXPECT_EQ((unsigned char)u[0], 0xEF);
    EXPECT_EQ((unsigned char)u[1], 0xBD);
    EXPECT_EQ((unsigned char)u[2], 0xA1);
    // 0xDF = ﾟ → U+FF9F → EF BE 9F（2 バイト目が繰り上がる境界）
    EXPECT_EQ(jis_kana_to_utf8(0xDF, u), 3);
    EXPECT_EQ((unsigned char)u[0], 0xEF);
    EXPECT_EQ((unsigned char)u[1], 0xBE);
    EXPECT_EQ((unsigned char)u[2], 0x9F);
}

TEST_F(OpDefFnTest, KanaToUtf8LeavesOtherBytesAlone) {
    char u[3];
    EXPECT_EQ(jis_kana_to_utf8('A', u), 0);
    EXPECT_EQ(jis_kana_to_utf8(0x20, u), 0);
    EXPECT_EQ(jis_kana_to_utf8(0xA0, u), 0); // カタカナ範囲の 1 つ手前
    EXPECT_EQ(jis_kana_to_utf8(0xE0, u), 0); // 1 つ後ろ
}

TEST_F(OpDefFnTest, KanaToUtf8IsContiguous) {
    // 0xA1-0xDF が U+FF61-U+FF9F へ順番どおり並ぶこと（抜けや重複がない）
    char u[3];
    for (int c = 0xA1; c <= 0xDF; c++) {
        ASSERT_EQ(jis_kana_to_utf8((unsigned char)c, u), 3) << c;
        unsigned int cp = 0xF000u | ((unsigned char)u[1] & 0x3F) << 6 | ((unsigned char)u[2] & 0x3F);
        EXPECT_EQ(cp, 0xFF61u + (unsigned int)(c - 0xA1)) << c;
    }
}

TEST_F(OpDefFnTest, LcdOutputKeepsRawKanaBytes) {
    // シリアルだけ変換し、LCD 側は 1 バイトのまま（フォントを引くため）
    mock_hal::reset();
    parse_and_execute(lex("PRINT \"\xB1\xB2\xB3\""));
    std::string out = mock_hal::get_raw_print_buffer();
    EXPECT_EQ(out, "\xB1\xB2\xB3\n");
}

// --- ACCEL() / GYRO() / IMU 文（QMI8658）-------------------------------
#include "hal_imu.h"

TEST_F(OpDefFnTest, ImuRawToMgUsesDatasheetSensitivity) {
    // フルスケール ±4g は 8192 LSB/g（データシートの感度表）
    EXPECT_EQ(imu_raw_to_mg(8192), 1000);   // +1G
    EXPECT_EQ(imu_raw_to_mg(-8192), -1000); // -1G
    EXPECT_EQ(imu_raw_to_mg(0), 0);
    EXPECT_EQ(imu_raw_to_mg(4096), 500);
}

TEST_F(OpDefFnTest, ImuRawToMgDoesNotOverflowAtFullScale) {
    // raw * 1000 が int に収まること（32767 * 1000 = 約 3276 万）
    EXPECT_EQ(imu_raw_to_mg(32767), 3999);
    EXPECT_EQ(imu_raw_to_mg(-32768), -4000);
}

TEST_F(OpDefFnTest, ImuRawToDpsUsesDatasheetSensitivity) {
    // フルスケール ±512dps は 64 LSB/dps
    EXPECT_EQ(imu_raw_to_dps(64), 1);
    EXPECT_EQ(imu_raw_to_dps(-64), -1);
    EXPECT_EQ(imu_raw_to_dps(32768 / 2), 256);
}

TEST_F(OpDefFnTest, AccelReturnsPerAxisValues) {
    hal_imu_set_mock(100, -200, 1000, 0, 0, 0);
    EXPECT_EQ(eval("ACCEL(0)"), "100\n");
    EXPECT_EQ(eval("ACCEL(1)"), "-200\n");
    EXPECT_EQ(eval("ACCEL(2)"), "1000\n");
}

TEST_F(OpDefFnTest, GyroReturnsPerAxisValues) {
    hal_imu_set_mock(0, 0, 0, 5, -10, 250);
    EXPECT_EQ(eval("GYRO(0)"), "5\n");
    EXPECT_EQ(eval("GYRO(1)"), "-10\n");
    EXPECT_EQ(eval("GYRO(2)"), "250\n");
}

TEST_F(OpDefFnTest, AccelAndGyroRejectBadAxis) {
    parse_and_execute(lex("PRINT ACCEL(3)"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("ACCEL argument must be"), std::string::npos);
    mock_hal::reset();
    parse_and_execute(lex("PRINT GYRO(-1)"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("GYRO argument must be"), std::string::npos);
}

TEST_F(OpDefFnTest, ImuStatementPrintsBothRows) {
    hal_imu_set_mock_present(true);
    hal_imu_set_mock(10, 20, 990, 1, 2, 3);
    mock_hal::reset();
    parse_and_execute(lex("IMU"));
    std::string out = mock_hal::get_raw_print_buffer();
    EXPECT_NE(out.find("ACCEL"), std::string::npos) << out;
    EXPECT_NE(out.find("990"), std::string::npos) << out;
    EXPECT_NE(out.find("GYRO"), std::string::npos) << out;
}

TEST_F(OpDefFnTest, ImuStatementReportsMissingChip) {
    hal_imu_set_mock_present(false);
    mock_hal::reset();
    parse_and_execute(lex("IMU"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("NOT FOUND"), std::string::npos);
    hal_imu_set_mock_present(true);
}

TEST_F(OpDefFnTest, ImuStatementWorksInProgram) {
    hal_imu_set_mock_present(true);
    hal_imu_set_mock(0, 0, 1000, 0, 0, 0);
    store_line(10, lex("IMU"));
    store_line(20, lex("PRINT \"AFTER\""));
    run("RUN");
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("AFTER"), std::string::npos);
}

TEST_F(OpDefFnTest, TiltCanDriveAConditional) {
    // 傾け操作の定番の書き方が動くこと
    hal_imu_set_mock(-400, 0, 900, 0, 0, 0);
    store_line(10, lex("X = 10"));
    store_line(20, lex("IF ACCEL(0) < -200 THEN X = X - 1"));
    store_line(30, lex("PRINT X"));
    run("RUN");
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("9"), std::string::npos);
}

// --- 端末からの半角カタカナ入力（UTF-8 → JIS）--------------------------
TEST_F(OpDefFnTest, Utf8ToJisKanaRoundTripsAllGlyphs) {
    // 出力側の変換を通した 3 バイトが、入力側で元の 1 バイトに戻ること
    for (int c = 0xA1; c <= 0xDF; c++) {
        char u[3];
        ASSERT_EQ(jis_kana_to_utf8((unsigned char)c, u), 3) << c;
        EXPECT_EQ(utf8_to_jis_kana((unsigned char)u[0], (unsigned char)u[1], (unsigned char)u[2]),
                  (unsigned char)c) << c;
    }
}

TEST_F(OpDefFnTest, Utf8ToJisKanaHandlesTheSecondHalf) {
    // ﾀ = U+FF80 = EF BE 80。ここが 2 バイト読み捨ての実装で壊れていた範囲
    EXPECT_EQ(utf8_to_jis_kana(0xEF, 0xBE, 0x80), 0xC0); // ﾀ
    EXPECT_EQ(utf8_to_jis_kana(0xEF, 0xBE, 0x81), 0xC1); // ﾁ
    EXPECT_EQ(utf8_to_jis_kana(0xEF, 0xBE, 0x82), 0xC2); // ﾂ
    EXPECT_EQ(utf8_to_jis_kana(0xEF, 0xBE, 0x9F), 0xDF); // ﾟ（範囲の末尾）
}

TEST_F(OpDefFnTest, Utf8ToJisKanaRejectsOtherCharacters) {
    // 漢字（漢 = U+6F22 = E6 BC A2）などは 0 を返す＝捨てる
    EXPECT_EQ(utf8_to_jis_kana(0xE6, 0xBC, 0xA2), 0);
    // 継続バイトが継続バイトでない
    EXPECT_EQ(utf8_to_jis_kana(0xEF, 0x41, 0x42), 0);
    // 3 バイト文字の 1 バイト目ではない
    EXPECT_EQ(utf8_to_jis_kana(0x41, 0x42, 0x43), 0);
    // 半角カタカナ範囲の 1 つ手前（U+FF60）
    EXPECT_EQ(utf8_to_jis_kana(0xEF, 0xBD, 0xA0), 0);
}

// --- RTC（PCF85063A: TIME$ / DATE$ / RTC 文）---------------------------
#include "hal_rtc.h"

class RtcTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
        mock_hal::reset();
        hal_rtc_init(); // モックを既定値に戻す
    }
    std::string eval(const char* expr) {
        mock_hal::reset();
        std::string cmd = std::string("PRINT ") + expr;
        parse_and_execute(lex(cmd.c_str()));
        return mock_hal::get_raw_print_buffer();
    }
};

TEST_F(RtcTest, BcdRoundTrips) {
    for (int v = 0; v <= 99; v++) EXPECT_EQ(bcd_to_int(int_to_bcd(v)), v) << v;
    EXPECT_EQ(int_to_bcd(59), 0x59); // BCD は 10 進の見た目そのまま
    EXPECT_EQ(bcd_to_int(0x23), 23);
}

TEST_F(RtcTest, LeapYearRules) {
    EXPECT_TRUE(rtc_is_leap(2024));
    EXPECT_FALSE(rtc_is_leap(2025));
    EXPECT_FALSE(rtc_is_leap(2100)); // 100 で割れる年は閏年でない
    EXPECT_TRUE(rtc_is_leap(2000));  // ただし 400 で割れる年は閏年
    EXPECT_EQ(rtc_days_in_month(2024, 2), 29);
    EXPECT_EQ(rtc_days_in_month(2025, 2), 28);
    EXPECT_EQ(rtc_days_in_month(2025, 4), 30);
}

TEST_F(RtcTest, WeekdayMatchesKnownDates) {
    EXPECT_EQ(rtc_weekday(2026, 8, 1), 6);  // 2026-08-01 は土曜
    EXPECT_EQ(rtc_weekday(2000, 1, 1), 6);  // 2000-01-01 も土曜
    EXPECT_EQ(rtc_weekday(2024, 2, 29), 4); // 閏日は木曜
}

TEST_F(RtcTest, ParseTimeAcceptsValidStrings) {
    int h = 0, m = 0, s = 0;
    EXPECT_TRUE(rtc_parse_time("00:00:00", &h, &m, &s));
    EXPECT_EQ(h, 0); EXPECT_EQ(m, 0); EXPECT_EQ(s, 0);
    EXPECT_TRUE(rtc_parse_time("23:59:59", &h, &m, &s));
    EXPECT_EQ(h, 23); EXPECT_EQ(m, 59); EXPECT_EQ(s, 59);
}

TEST_F(RtcTest, ParseTimeRejectsBadStrings) {
    int h, m, s;
    EXPECT_FALSE(rtc_parse_time("24:00:00", &h, &m, &s)); // 時が範囲外
    EXPECT_FALSE(rtc_parse_time("12:60:00", &h, &m, &s)); // 分が範囲外
    EXPECT_FALSE(rtc_parse_time("12:00",    &h, &m, &s)); // 桁数不足
    EXPECT_FALSE(rtc_parse_time("12-00-00", &h, &m, &s)); // 区切りが違う
    EXPECT_FALSE(rtc_parse_time("1X:00:00", &h, &m, &s)); // 数字でない
}

TEST_F(RtcTest, ParseDateAcceptsValidStrings) {
    int y, mo, d;
    EXPECT_TRUE(rtc_parse_date("2026-08-01", &y, &mo, &d));
    EXPECT_EQ(y, 2026); EXPECT_EQ(mo, 8); EXPECT_EQ(d, 1);
    EXPECT_TRUE(rtc_parse_date("2024-02-29", &y, &mo, &d)); // 閏日
}

TEST_F(RtcTest, ParseDateRejectsBadStrings) {
    int y, mo, d;
    EXPECT_FALSE(rtc_parse_date("2025-02-29", &y, &mo, &d)); // 閏年でない 2/29
    EXPECT_FALSE(rtc_parse_date("2026-13-01", &y, &mo, &d)); // 月が範囲外
    EXPECT_FALSE(rtc_parse_date("2026-04-31", &y, &mo, &d)); // 4 月は 30 日まで
    EXPECT_FALSE(rtc_parse_date("1999-01-01", &y, &mo, &d)); // チップは 2000 年から
    EXPECT_FALSE(rtc_parse_date("2100-01-01", &y, &mo, &d)); // 2099 年まで
    EXPECT_FALSE(rtc_parse_date("2026/08/01", &y, &mo, &d)); // 区切りが違う
}

TEST_F(RtcTest, TimeAndDateStringsAreRead) {
    RtcTime t{2026, 8, 1, 9, 5, 3, true};
    hal_rtc_set_mock(&t);
    EXPECT_EQ(eval("TIME$"), "09:05:03\n"); // 0 詰めされる
    EXPECT_EQ(eval("DATE$"), "2026-08-01\n");
}

TEST_F(RtcTest, TimeCanBeSet) {
    parse_and_execute(lex("TIME$ = \"14:30:00\""));
    EXPECT_EQ(eval("TIME$"), "14:30:00\n");
}

TEST_F(RtcTest, DateCanBeSet) {
    parse_and_execute(lex("DATE$ = \"2027-12-25\""));
    EXPECT_EQ(eval("DATE$"), "2027-12-25\n");
}

TEST_F(RtcTest, SettingOneKeepsTheOther) {
    RtcTime t{2026, 8, 1, 9, 5, 3, true};
    hal_rtc_set_mock(&t);
    parse_and_execute(lex("TIME$ = \"23:00:00\""));
    EXPECT_EQ(eval("DATE$"), "2026-08-01\n"); // 日付は変わらない
}

// 弾く理由（書式 / 範囲 / 実在しない日付）ごとに別のメッセージを出すこと。
// 一律に書式のエラーを出すと "2025-02-29" の原因を取り違えさせる
TEST_F(RtcTest, RejectsBadFormatWithFormatMessage) {
    parse_and_execute(lex("DATE$ = \"2026/08/01\""));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("YYYY-MM-DD"), std::string::npos);
    mock_hal::reset();
    parse_and_execute(lex("TIME$ = \"14:30\""));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("HH:MM:SS"), std::string::npos);
}

TEST_F(RtcTest, RejectsOutOfRangeTimeWithRangeMessage) {
    // 書式は正しいので「範囲外」と伝える
    parse_and_execute(lex("TIME$ = \"25:00:00\""));
    std::string out = mock_hal::get_raw_print_buffer();
    EXPECT_NE(out.find("out of range"), std::string::npos) << out;
}

TEST_F(RtcTest, RejectsOutOfRangeYearWithYearMessage) {
    parse_and_execute(lex("DATE$ = \"1999-01-01\""));
    std::string out = mock_hal::get_raw_print_buffer();
    EXPECT_NE(out.find("2000 to 2099"), std::string::npos) << out;
}

TEST_F(RtcTest, RejectsImpossibleDateWithItsOwnMessage) {
    // 書式も年も正しく、暦の上で存在しないだけ
    parse_and_execute(lex("DATE$ = \"2025-02-29\""));
    std::string out = mock_hal::get_raw_print_buffer();
    EXPECT_NE(out.find("No such date"), std::string::npos) << out;
    mock_hal::reset();
    parse_and_execute(lex("DATE$ = \"2026-04-31\""));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("No such date"), std::string::npos);
}

TEST_F(RtcTest, RejectedSettingLeavesTheClockAlone) {
    RtcTime t{2026, 8, 1, 9, 5, 3, true};
    hal_rtc_set_mock(&t);
    parse_and_execute(lex("DATE$ = \"2025-02-29\""));
    EXPECT_EQ(eval("DATE$"), "2026-08-01\n"); // 時計は変わっていない
}

TEST_F(RtcTest, SettingClearsTheNotSetFlag) {
    // 電源断で時刻が飛んだ状態を作る
    RtcTime bad{2000, 1, 1, 0, 0, 0, false};
    hal_rtc_set_mock(&bad);
    mock_hal::reset();
    parse_and_execute(lex("RTC"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("NOT SET"), std::string::npos);
    // 時刻を入れると有効になる
    parse_and_execute(lex("DATE$ = \"2026-08-01\""));
    mock_hal::reset();
    parse_and_execute(lex("RTC"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("CLOCK OK"), std::string::npos);
}

TEST_F(RtcTest, RtcStatementPrintsDateAndTime) {
    RtcTime t{2026, 8, 1, 9, 5, 3, true};
    hal_rtc_set_mock(&t);
    mock_hal::reset();
    parse_and_execute(lex("RTC"));
    std::string out = mock_hal::get_raw_print_buffer();
    EXPECT_NE(out.find("2026-08-01 09:05:03"), std::string::npos) << out;
}

TEST_F(RtcTest, RtcStatementReportsMissingChip) {
    hal_rtc_set_mock_present(false);
    mock_hal::reset();
    parse_and_execute(lex("RTC"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("NOT FOUND"), std::string::npos);
    hal_rtc_set_mock_present(true);
}

TEST_F(RtcTest, TimeStampCanBeWrittenToAFile) {
    // 保存日時を残す、という実際の使い方が通ること
    RtcTime t{2026, 8, 1, 9, 5, 3, true};
    hal_rtc_set_mock(&t);
    store_line(10, lex("OPEN \"STAMP.DAT\" FOR OUTPUT AS #1"));
    store_line(20, lex("PRINT #1, DATE$, TIME$"));
    store_line(30, lex("CLOSE #1"));
    store_line(40, lex("OPEN \"STAMP.DAT\" FOR INPUT AS #1"));
    store_line(50, lex("INPUT #1, D$, T$"));
    store_line(60, lex("CLOSE #1"));
    store_line(70, lex("PRINT D$; \" \"; T$"));
    parse_and_execute(lex("RUN"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("2026-08-01 09:05:03"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

// --- RANDOMIZE と RUN 時の自動シード ------------------------------------
TEST_F(OpDefFnTest, RandomizeWithSeedIsReproducible) {
    // 同じ種を指定すれば同じ列になる（デバッグ用途）
    parse_and_execute(lex("RANDOMIZE 12345"));
    mock_hal::reset();
    parse_and_execute(lex("PRINT RND(1000)"));
    std::string first = mock_hal::get_raw_print_buffer();

    parse_and_execute(lex("RANDOMIZE 12345"));
    mock_hal::reset();
    parse_and_execute(lex("PRINT RND(1000)"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), first);
}

TEST_F(OpDefFnTest, DifferentSeedsGiveDifferentSequences) {
    parse_and_execute(lex("RANDOMIZE 1"));
    mock_hal::reset();
    parse_and_execute(lex("PRINT RND(100000)"));
    std::string a = mock_hal::get_raw_print_buffer();

    parse_and_execute(lex("RANDOMIZE 999"));
    mock_hal::reset();
    parse_and_execute(lex("PRINT RND(100000)"));
    EXPECT_NE(mock_hal::get_raw_print_buffer(), a);
}

TEST_F(OpDefFnTest, RandomizeWithoutArgumentDoesNotError) {
    // 引数なしは時刻から種を作る。値は検証できないので通ることだけ見る
    parse_and_execute(lex("RANDOMIZE"));
    mock_hal::reset();
    parse_and_execute(lex("PRINT RND(10)"));
    EXPECT_FALSE(mock_hal::get_raw_print_buffer().empty());
}

TEST_F(OpDefFnTest, RandomizeWorksInsideProgram) {
    store_line(10, lex("RANDOMIZE 42"));
    store_line(20, lex("PRINT RND(1000)"));
    run("RUN");
    std::string first = mock_hal::get_raw_print_buffer();
    mock_hal::reset();
    run("RUN");
    // プログラム内で種を固定しているので、RUN の自動シードに勝つ
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), first);
}

TEST_F(OpDefFnTest, SeedSourceIsNeverZero) {
    // 0 を種にすると実装によっては列が退化するので避けている
    EXPECT_NE(basic_random_seed_source(), 0u);
}

TEST_F(OpDefFnTest, ExplicitRndSeedStillOverridesAutoSeed) {
    // 従来の RND(-n) による再現も引き続き使える
    store_line(10, lex("X = RND(-7)"));
    store_line(20, lex("PRINT RND(1000)"));
    run("RUN");
    std::string first = mock_hal::get_raw_print_buffer();
    mock_hal::reset();
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), first);
}

TEST_F(OpDefFnTest, RandomizeSurvivesListRoundTrip) {
    store_line(10, lex("RANDOMIZE 5"));
    mock_hal::reset();
    run("LIST");
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("RANDOMIZE"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

// --- SYNC（画面転送の遅延／ちらつき防止）--------------------------------
#include "hal_display.h"
TEST_F(OpDefFnTest, SyncOffEnablesDeferredMode) {
    parse_and_execute(lex("SYNC OFF"));
    EXPECT_TRUE(hal_display_is_deferred());
}

TEST_F(OpDefFnTest, SyncOnRestoresImmediateMode) {
    parse_and_execute(lex("SYNC OFF"));
    parse_and_execute(lex("SYNC ON"));
    EXPECT_FALSE(hal_display_is_deferred());
}

TEST_F(OpDefFnTest, BareSyncFlushesWithoutLeavingDeferredMode) {
    parse_and_execute(lex("SYNC OFF"));
    mock_hal::reset_flush_count();
    parse_and_execute(lex("SYNC"));
    EXPECT_EQ(mock_hal::get_flush_count(), 1);
    EXPECT_TRUE(hal_display_is_deferred()); // 引数なしは溜め込みを続ける
    parse_and_execute(lex("SYNC ON"));
}

TEST_F(OpDefFnTest, SyncOnFlushesWhatWasPending) {
    parse_and_execute(lex("SYNC OFF"));
    mock_hal::reset_flush_count();
    parse_and_execute(lex("SYNC ON"));
    EXPECT_EQ(mock_hal::get_flush_count(), 1); // 戻すときに出し切る
}

TEST_F(OpDefFnTest, ProgramEndAlwaysRestoresImmediateMode) {
    // SYNC OFF のまま終わると画面が固まって見えるので、必ず解除される
    store_line(10, lex("SYNC OFF"));
    store_line(20, lex("PSET (10,10), 15"));
    run("RUN");
    EXPECT_FALSE(hal_display_is_deferred());
}

TEST_F(OpDefFnTest, ErrorInsideProgramAlsoRestoresImmediateMode) {
    store_line(10, lex("SYNC OFF"));
    store_line(20, lex("X = 1 / 0")); // ここでエラー
    run("RUN");
    EXPECT_FALSE(hal_display_is_deferred());
}

TEST_F(OpDefFnTest, DrawEraseRedrawPatternWorksUnderSync) {
    // ちらつき防止の定番の書き方が通ること
    store_line(10, lex("SYNC OFF"));
    store_line(20, lex("CIRCLE (100,100), 5, 0"));
    store_line(30, lex("CIRCLE (110,100), 5, 14"));
    store_line(40, lex("SYNC"));
    store_line(50, lex("PRINT \"DONE\""));
    run("RUN");
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("DONE"), std::string::npos);
}

TEST_F(OpDefFnTest, SyncSurvivesListRoundTrip) {
    store_line(10, lex("SYNC OFF"));
    store_line(20, lex("SYNC ON"));
    mock_hal::reset();
    run("LIST");
    std::string out = mock_hal::get_raw_print_buffer();
    EXPECT_NE(out.find("SYNC OFF"), std::string::npos) << out;
    EXPECT_NE(out.find("SYNC ON"), std::string::npos) << out;
}

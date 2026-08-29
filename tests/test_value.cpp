#include <gtest/gtest.h>
#include "parser_internal.h"
#include <cstring>

// Value の値表現。式評価が値渡しで何度も複製する型なので、
// コピーの挙動と c_str() の有効期間をここで押さえておく。

// c_str() が返すのは Value ごとの領域であること。
//
// 書式化先が関数内の static なバッファだと、この 2 つのポインタが同じ場所を
// 指し、先に取った方の内容が後の書式化で消える
// （printf("%s %s", a.c_str(), b.c_str()) が "2 2" になる）。
TEST(ValueTest, CStrOfTwoNumbersDoesNotAlias) {
    Value a(1);
    Value b(2);
    const char* pa = a.c_str();
    const char* pb = b.c_str();

    EXPECT_NE(pa, pb);
    EXPECT_STREQ(pa, "1");
    EXPECT_STREQ(pb, "2");
}

// 1 つの式の中で 2 回評価しても、それぞれの値が出ること
TEST(ValueTest, CStrSurvivesBeingUsedTwiceInOneCall) {
    Value a(10);
    Value b(20);
    char out[32];
    snprintf(out, sizeof(out), "%s %s", a.c_str(), b.c_str());
    EXPECT_STREQ(out, "10 20");
}

TEST(ValueTest, CStrFormatsIntegersWithoutADecimalPoint) {
    EXPECT_STREQ(Value(42).c_str(), "42");
}

TEST(ValueTest, CStrFormatsFloats) {
    EXPECT_STREQ(Value(1.5f).c_str(), "1.5");
}

TEST(ValueTest, CStrOfAStringReturnsTheStringItself) {
    Value v("HELLO");
    EXPECT_STREQ(v.c_str(), "HELLO");
}

// 数値の複製で 128 バイトの str_val まで写さない最適化を壊していないこと。
// c_str() が str_val を書き換えるようになったので、複製した側が元の書式化結果を
// 引きずらないことも同時に確かめる
TEST(ValueTest, CopyingANumberDoesNotCarryTheFormattedText) {
    Value a(7);
    a.c_str(); // str_val に "7" が入る
    Value b = a;
    EXPECT_EQ(b.str_val[0], '\0');
    EXPECT_STREQ(b.c_str(), "7"); // 必要になったら自分で書式化する
}

TEST(ValueTest, CopyingAStringKeepsTheText) {
    Value a("WORLD");
    Value b = a;
    EXPECT_STREQ(b.c_str(), "WORLD");
}

#include <gtest/gtest.h>
#include "strutil.h"
#include <cstring>

// src/strutil.h の 2 つのコピー。インタプリタ中の固定長バッファはほぼ全部
// これを通るので、境界の挙動をここで押さえておく。

TEST(StrUtilTest, CopyStringCopiesWhatFits) {
    char dst[8];
    copy_string(dst, sizeof(dst), "ABC");
    EXPECT_STREQ(dst, "ABC");
}

TEST(StrUtilTest, CopyStringTruncatesAndAlwaysTerminates) {
    char dst[4];
    copy_string(dst, sizeof(dst), "ABCDEFG");
    EXPECT_STREQ(dst, "ABC"); // 3 文字 + 終端
}

// ちょうど収まる長さで終端が落ちないこと（strncpy が終端しなくなる境界）
TEST(StrUtilTest, CopyStringTerminatesAtExactFit) {
    char dst[4];
    copy_string(dst, sizeof(dst), "ABC");
    EXPECT_STREQ(dst, "ABC");
    EXPECT_EQ(dst[3], '\0');
}

TEST(StrUtilTest, CopyStringHandlesEmptySource) {
    char dst[4] = "XYZ";
    copy_string(dst, sizeof(dst), "");
    EXPECT_STREQ(dst, "");
}

// 大きさ 0 のバッファには何も書かない（書いたら 1 バイト踏み越える）
TEST(StrUtilTest, CopyStringWritesNothingIntoZeroSizedBuffer) {
    char guard[2] = { 'A', 'B' };
    copy_string(guard, 0, "ZZZ");
    EXPECT_EQ(guard[0], 'A');
    EXPECT_EQ(guard[1], 'B');
}

// 固定長欄は終端を持たず、余りは 0 で埋める（論理メモリ上の 8 バイト変数名）
TEST(StrUtilTest, CopyFixedFieldZeroFillsTheRemainder) {
    char field[8];
    memset(field, 'X', sizeof(field));
    copy_fixed_field(field, sizeof(field), "AB");
    EXPECT_EQ(memcmp(field, "AB\0\0\0\0\0\0", 8), 0);
}

// ちょうど埋まる名前は 8 バイトすべてを使い、終端は書かない
TEST(StrUtilTest, CopyFixedFieldFillsTheWholeFieldWithoutTerminator) {
    char field[9];
    field[8] = '!'; // 欄の外。触られないこと
    copy_fixed_field(field, 8, "ABCDEFGH");
    EXPECT_EQ(memcmp(field, "ABCDEFGH", 8), 0);
    EXPECT_EQ(field[8], '!');
}

// 欄より長い名前は切り詰める（欄の外へはみ出さない）
TEST(StrUtilTest, CopyFixedFieldTruncatesLongerNames) {
    char field[9];
    field[8] = '!';
    copy_fixed_field(field, 8, "ABCDEFGHIJK");
    EXPECT_EQ(memcmp(field, "ABCDEFGH", 8), 0);
    EXPECT_EQ(field[8], '!');
}

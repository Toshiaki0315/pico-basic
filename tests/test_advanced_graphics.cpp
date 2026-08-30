#include <gtest/gtest.h>
#include "parser.h"
#include "hal_display.h"
#include "mock_hal_display.h"
#include "lexer.h"
#include <string>

class AdvancedGraphicsTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_hal::reset();
        clear_program();
    }

    TokenList lex_tokens(const char* input) {
        return lex(input);
    }
};

TEST_F(AdvancedGraphicsTest, PaintCommand) {
    // Draw a rectangle border
    parse_and_execute(lex_tokens("COLOR 1")); // Blue
    parse_and_execute(lex_tokens("LINE 0,0,10,0"));
    parse_and_execute(lex_tokens("LINE 10,0,10,10"));
    parse_and_execute(lex_tokens("LINE 10,10,0,10"));
    parse_and_execute(lex_tokens("LINE 0,10,0,0"));
    
    // Paint inside
    parse_and_execute(lex_tokens("PAINT (5,5), 2")); // Green
    
    // Verify interior pixel
    EXPECT_EQ(hal_graphics_get_pixel(5, 5), 0x07E0); // PALETTE[2] (Green)
    EXPECT_EQ(hal_graphics_get_pixel(0, 0), 0x001F); // PALETTE[1] (Blue) - Border
}

TEST_F(AdvancedGraphicsTest, GetAtPutAt) {
    // 1. Draw something
    parse_and_execute(lex_tokens("PSET 5,5,1"));
    parse_and_execute(lex_tokens("PSET 6,5,2"));
    
    // 2. Capture to array
    parse_and_execute(lex_tokens("DIM A(10)"));
    parse_and_execute(lex_tokens("GET_AT (5,5)-(6,5), A"));
    
    // 3. Clear and Put at new location
    parse_and_execute(lex_tokens("CLS"));
    EXPECT_EQ(hal_graphics_get_pixel(5, 5), 0);
    
    parse_and_execute(lex_tokens("PUT_AT (10,10), A"));
    
    // 4. Verify
    EXPECT_EQ(hal_graphics_get_pixel(10, 10), 0x001F); // Blue
    EXPECT_EQ(hal_graphics_get_pixel(11, 10), 0x07E0); // Green
}

// ---------------------------------------------------------
// GET@ / PUT@ の書式
//
// マニュアル記載の `GET@` / `PUT@` は字句解析すら通らず、
// 「Invalid variable name」で落ちていた（'@' が識別子文字のため
// "GET@" ひと続きの変数名として扱われていた）。
// ---------------------------------------------------------

TEST_F(AdvancedGraphicsTest, GetAtAcceptsAtSignForm) {
    parse_and_execute(lex_tokens("DIM A(1000)"));
    parse_and_execute(lex_tokens("PSET (5,5), 15"));
    parse_and_execute(lex_tokens("GET@ (5,5)-(6,5), A"));

    // 以前は '@' が識別子文字のため "GET@" が変数名として扱われ、
    // 「Invalid variable name」で例外になっていた
    EXPECT_EQ(mock_hal::get_raw_print_buffer().find("Invalid variable name"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().empty())
        << "エラーが出ている: " << mock_hal::get_raw_print_buffer();
}

TEST_F(AdvancedGraphicsTest, PutAtAcceptsAtSignForm) {
    parse_and_execute(lex_tokens("DIM A(1000)"));
    parse_and_execute(lex_tokens("PSET (5,5), 15"));
    parse_and_execute(lex_tokens("GET@ (5,5)-(6,5), A"));
    mock_hal::reset();

    parse_and_execute(lex_tokens("PUT@ (10,10), A"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer().find("Syntax Error"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

TEST_F(AdvancedGraphicsTest, GetAtRoundTripsPixels) {
    parse_and_execute(lex_tokens("DIM A(1000)"));
    parse_and_execute(lex_tokens("PSET (5,5), 15"));
    parse_and_execute(lex_tokens("GET@ (5,5)-(6,6), A"));
    parse_and_execute(lex_tokens("PUT@ (100,100), A"));

    // 取り込んだ点が転送先にも現れる
    EXPECT_NE(hal_graphics_get_pixel(100, 100), 0)
        << "PUT@ で画素が復元されていない: " << mock_hal::get_raw_print_buffer();
}

TEST_F(AdvancedGraphicsTest, GetAtReportsArrayTooSmall) {
    parse_and_execute(lex_tokens("DIM B(10)"));
    parse_and_execute(lex_tokens("GET@ (0,0)-(60,60), B"));

    EXPECT_NE(mock_hal::get_raw_print_buffer().find("Array too small for image"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

// ---------------------------------------------------------
// RUN のたびの初期化
//
// RUN が変数・配列を初期化していなかったため、DIM を含むプログラムを
// 2 回実行すると配列ヒープを二重に消費して Out of Memory になっていた。
// ---------------------------------------------------------

TEST_F(AdvancedGraphicsTest, RunTwiceDoesNotExhaustArrayHeap) {
    store_line(10, lex_tokens("DIM A(1000)"));
    store_line(20, lex_tokens("A(0) = 7"));

    run_program();
    mock_hal::reset();
    run_program(); // 2 回目

    EXPECT_EQ(mock_hal::get_raw_print_buffer().find("Out of Memory"), std::string::npos)
        << "2 回目の RUN でヒープが尽きている: " << mock_hal::get_raw_print_buffer();
}

TEST_F(AdvancedGraphicsTest, RunClearsVariablesFromPreviousRun) {
    parse_and_execute(lex_tokens("Z = 99"));
    store_line(10, lex_tokens("PRINT Z"));

    run_program();
    // RUN で変数が初期化されるので、直前に代入した 99 は残らない
    EXPECT_EQ(mock_hal::get_raw_print_buffer().find("99"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

TEST_F(AdvancedGraphicsTest, RedimSameArrayIsRejected) {
    parse_and_execute(lex_tokens("DIM A(100)"));
    mock_hal::reset();
    parse_and_execute(lex_tokens("DIM A(100)"));

    EXPECT_NE(mock_hal::get_raw_print_buffer().find("Duplicate definition"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

// ---------------------------------------------------------
// PUT@ の描画モードと拡大縮小
// ---------------------------------------------------------

// A に w×h の画像を入れるヘルパ（全画素同じ色）
static void fill_sprite(const char* dimline, int w, int h, int color_idx) {
    parse_and_execute(lex(dimline));
    // GET@ で取り込むのが本筋だが、ここでは PSET で作った矩形を取り込む
    char buf[64];
    // 画面に w×h の矩形を描いて GET@ で吸い出す
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            snprintf(buf, sizeof(buf), "PSET (%d,%d), %d", x, y, color_idx);
            parse_and_execute(lex(buf));
        }
    snprintf(buf, sizeof(buf), "GET@ (0,0)-(%d,%d), A", w - 1, h - 1);
    parse_and_execute(lex(buf));
}

TEST_F(AdvancedGraphicsTest, PutAtDefaultOverwrites) {
    fill_sprite("DIM A(200)", 4, 4, 15); // 白
    parse_and_execute(lex("PUT@ (100,100), A"));

    EXPECT_EQ(hal_graphics_get_pixel(100, 100), hal_graphics_get_pixel(0, 0))
        << "等倍 PUT@ で色が復元されない";
}

TEST_F(AdvancedGraphicsTest, PutAtXorErasesOnSecondPut) {
    fill_sprite("DIM A(200)", 4, 4, 15);

    // XOR(mode 3) で 2 回置くと元の背景（黒 0）に戻る
    parse_and_execute(lex("PUT@ (100,100), A, 3"));
    uint16_t after_first = hal_graphics_get_pixel(100, 100);
    parse_and_execute(lex("PUT@ (100,100), A, 3"));
    uint16_t after_second = hal_graphics_get_pixel(100, 100);

    EXPECT_NE(after_first, 0) << "1 回目の XOR で描画されていない";
    EXPECT_EQ(after_second, 0) << "2 回目の XOR で消えていない（スプライト消去にならない）";
}

TEST_F(AdvancedGraphicsTest, PutAtScalesToDestRect) {
    fill_sprite("DIM A(200)", 2, 2, 15); // 2x2 の画像

    // 転送先を 10x10 に拡大
    parse_and_execute(lex("PUT@ (100,100)-(109,109), A"));

    // 拡大先の四隅と中央が塗られている
    EXPECT_NE(hal_graphics_get_pixel(100, 100), 0);
    EXPECT_NE(hal_graphics_get_pixel(109, 109), 0) << "拡大先の右下端が塗られていない";
    EXPECT_NE(hal_graphics_get_pixel(105, 105), 0);
}

// --- GET@ / PUT@ の寸法検証 ---------------------------------------------
//
// 寸法は掛ける前に確かめる必要がある。w * h を先に計算すると int が溢れ、
// 「配列が小さい」判定を素通りしたループが論理メモリ全体を上書きしていた。
// 下のテストはどれも、直っていなければ戻ってこない（= タイムアウトで落ちる）。

TEST_F(AdvancedGraphicsTest, GetAtRejectsRectangleLargerThanScreen) {
    parse_and_execute(lex("DIM A(10)"));
    mock_hal::reset();
    // w = h = 65536。65536 * 65536 は int で 0 に折り返す
    parse_and_execute(lex("GET@ (0,0)-(65535,65535), A"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("larger than the screen"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

// 溢れない大きさでも、画面より広ければ受け付けない。
// 辺の判定は配列の大きさの判定より先に来る（どちらの理由で断ったか分かるように）
TEST_F(AdvancedGraphicsTest, GetAtRejectsModestlyOversizedRectangle) {
    parse_and_execute(lex("DIM A(10)"));
    mock_hal::reset();
    parse_and_execute(lex("GET@ (0,0)-(400,300), A"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("larger than the screen"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

// 画面に収まる矩形はこれまでどおり通る。
// 配列ヒープは 0x9000-0xC000 の 12KB で 1 要素 8 バイトなので 1536 要素まで。
// GET@ は 1 画素に 1 要素使うため、取り込めるのは 38x38 程度が上限になる
TEST_F(AdvancedGraphicsTest, GetAtAcceptsRectangleThatFits) {
    parse_and_execute(lex("DIM A(950)"));
    mock_hal::reset();
    parse_and_execute(lex("GET@ (0,0)-(29,29), A")); // 30x30 = 900 画素
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "") << mock_hal::get_raw_print_buffer();
}

// 画像の寸法は配列の先頭 2 要素そのものなので BASIC から書き換えられる
TEST_F(AdvancedGraphicsTest, PutAtRejectsForgedImageSize) {
    parse_and_execute(lex("DIM A(10)"));
    parse_and_execute(lex("A(0) = 30000"));
    parse_and_execute(lex("A(1) = 30000"));
    mock_hal::reset();
    parse_and_execute(lex("PUT@ (0,0), A"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("image size is not valid"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

TEST_F(AdvancedGraphicsTest, PutAtRejectsDestinationLargerThanScreen) {
    parse_and_execute(lex("DIM A(200)"));
    parse_and_execute(lex("GET@ (0,0)-(7,7), A")); // 正しい 8x8 の画像を作る
    mock_hal::reset();
    parse_and_execute(lex("PUT@ (0,0)-(9000,9000), A"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("larger than the screen"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

// 取り込んだ画像がそのまま戻せること（ガードで壊していないことの確認）
TEST_F(AdvancedGraphicsTest, GetAtPutAtStillRoundTrips) {
    parse_and_execute(lex("DIM A(200)"));
    parse_and_execute(lex("LINE (0,0)-(7,7), 12, BF"));
    parse_and_execute(lex("GET@ (0,0)-(7,7), A"));
    mock_hal::reset();
    parse_and_execute(lex("PUT@ (100,100), A"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer().find("Illegal function call"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

// --- 画像の詰め方と容量 ---------------------------------------------------
//
// 画素は 16bit だが配列の 1 要素は 8 バイトある。1 画素 1 要素だと 4 倍の
// 場所を食い、12KB のヒープでは 39x39 ほどしか入らなかった。画素だけ 2 バイト
// ずつ詰め、あわせて本文領域を削って配列ヒープを 20KB にしてある。

// 詰めた結果 1 要素に 4 画素入る。DIM A(10) は 11 要素 = 88 バイトなので
// 先頭 2 要素を除いた 72 バイト = 36 画素まで
TEST_F(AdvancedGraphicsTest, GetAtFitsFourPixelsPerArrayElement) {
    parse_and_execute(lex("DIM A(10)"));
    mock_hal::reset();
    parse_and_execute(lex("GET@ (0,0)-(5,5), A")); // 6x6 = 36 画素
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "") << mock_hal::get_raw_print_buffer();
}

TEST_F(AdvancedGraphicsTest, GetAtRefusesOnePixelBeyondCapacity) {
    parse_and_execute(lex("DIM A(10)"));
    mock_hal::reset();
    parse_and_execute(lex("GET@ (0,0)-(6,5), A")); // 7x6 = 42 画素 > 36
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("Array too small"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

// 以前の上限（39x39）を超える画像が取り込めること
TEST_F(AdvancedGraphicsTest, GetAtHandlesImagesLargerThanTheOldLimit) {
    parse_and_execute(lex("DIM A(1300)")); // 64x64 = 4096 画素に足りる
    mock_hal::reset();
    parse_and_execute(lex("GET@ (0,0)-(63,63), A"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "") << mock_hal::get_raw_print_buffer();
}

// 幅・高さは今までどおり先頭 2 要素から BASIC で読める（MANUAL の約束）
TEST_F(AdvancedGraphicsTest, GetAtStillExposesWidthAndHeightToBasic) {
    parse_and_execute(lex("DIM A(50)"));
    parse_and_execute(lex("GET@ (0,0)-(9,4), A")); // 10x5
    mock_hal::reset();
    parse_and_execute(lex("PRINT A(0); A(1)"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("10"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("5"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

// 取り込んだ色がそのまま戻ること（詰め方を間違えると色が化ける）。
//
// mock_hal::reset() はフレームバッファも消すので、描いてから読むまでの間で
// 呼べない。出力は CaptureStdout で受ける
TEST_F(AdvancedGraphicsTest, GetAtPutAtPreservesPixelColours) {
    parse_and_execute(lex("DIM A(50)"));
    parse_and_execute(lex("PSET (0,0), 12"));
    parse_and_execute(lex("PSET (3,3), 9"));
    parse_and_execute(lex("GET@ (0,0)-(3,3), A"));
    parse_and_execute(lex("PUT@ (100,100), A"));

    testing::internal::CaptureStdout();
    parse_and_execute(lex("PRINT POINT(100,100)"));
    parse_and_execute(lex("PRINT POINT(103,103)"));
    std::string out = testing::internal::GetCapturedStdout();
    EXPECT_EQ(out, "12\n9\n") << out; // 左上と右下の色がそのまま
}

// 4 画素を 1 要素に詰めるので、要素の境界をまたぐ位置でも色が化けないこと
TEST_F(AdvancedGraphicsTest, GetAtPutAtPreservesColoursAcrossPackingBoundary) {
    parse_and_execute(lex("DIM A(50)"));
    for (int i = 0; i < 6; i++) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "PSET (%d,0), %d", i, i + 9);
        parse_and_execute(lex(cmd));
    }
    parse_and_execute(lex("GET@ (0,0)-(5,0), A")); // 6 画素 = 1.5 要素ぶん
    parse_and_execute(lex("PUT@ (200,50), A"));

    testing::internal::CaptureStdout();
    for (int i = 0; i < 6; i++) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "PRINT POINT(%d,50)", 200 + i);
        parse_and_execute(lex(cmd));
    }
    std::string out = testing::internal::GetCapturedStdout();
    EXPECT_EQ(out, "9\n10\n11\n12\n13\n14\n") << out;
}

// 本文領域を削ったぶん、以前は入らなかった大きさの配列が取れる
// （配列ヒープ 12KB = 1536 要素 -> 20KB = 2560 要素）
TEST_F(AdvancedGraphicsTest, ArrayHeapHoldsMoreThanTheOldSixteenHundred) {
    mock_hal::reset();
    parse_and_execute(lex("DIM A(2000)"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "") << mock_hal::get_raw_print_buffer();
}

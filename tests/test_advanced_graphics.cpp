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

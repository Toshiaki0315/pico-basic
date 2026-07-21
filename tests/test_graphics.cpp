#include <algorithm>
#include <gtest/gtest.h>
#include "parser.h"
#include "mock_hal_display.h"
#include "lexer.h"
#include "parser_internal.h" // reset_graphics_window()
#include <string>

class GraphicsTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_hal::reset();
        clear_program();
    }

    TokenList lex_tokens(const char* input) {
        return lex(input);
    }
};

TEST_F(GraphicsTest, ColorCommand) {
    parse_and_execute(lex_tokens("COLOR 2"));
    parse_and_execute(lex_tokens("PSET 10,10"));
    
    auto cmds = mock_hal::get_draw_commands();
    ASSERT_EQ(cmds.size(), 1);
    EXPECT_EQ(cmds[0].type, DrawCommand::PSET);
    EXPECT_EQ(cmds[0].color, 0x07E0); // Green (Palette 2)
}

TEST_F(GraphicsTest, PsetCommandWithExplicitColor) {
    parse_and_execute(lex_tokens("PSET 50,60,4"));
    
    auto cmds = mock_hal::get_draw_commands();
    ASSERT_EQ(cmds.size(), 1);
    EXPECT_EQ(cmds[0].x1, 50);
    EXPECT_EQ(cmds[0].y1, 60);
    EXPECT_EQ(cmds[0].color, 0xF800); // Red (Palette 4)
}

TEST_F(GraphicsTest, LineCommand) {
    parse_and_execute(lex_tokens("LINE 0,0,100,100,14"));
    
    auto cmds = mock_hal::get_draw_commands();
    ASSERT_EQ(cmds.size(), 1);
    EXPECT_EQ(cmds[0].type, DrawCommand::LINE);
    EXPECT_EQ(cmds[0].x1, 0);
    EXPECT_EQ(cmds[0].y1, 0);
    EXPECT_EQ(cmds[0].x2, 100);
    EXPECT_EQ(cmds[0].y2, 100);
    EXPECT_EQ(cmds[0].color, 0xFFE0); // Yellow (Palette 14)
}

TEST_F(GraphicsTest, CircleCommand) {
    parse_and_execute(lex_tokens("CIRCLE 160,120,50,1"));
    
    auto cmds = mock_hal::get_draw_commands();
    ASSERT_EQ(cmds.size(), 1);
    EXPECT_EQ(cmds[0].type, DrawCommand::CIRCLE);
    EXPECT_EQ(cmds[0].x1, 160);
    EXPECT_EQ(cmds[0].y1, 120);
    EXPECT_EQ(cmds[0].r, 50);
    EXPECT_EQ(cmds[0].color, 0x001F); // Blue (Palette 1)
}

TEST_F(GraphicsTest, InvalidColorIndex) {
    parse_and_execute(lex_tokens("COLOR 20"));
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().find("Invalid color index") != std::string::npos);
}

TEST_F(GraphicsTest, ClsCommand) {
    parse_and_execute(lex_tokens("CLS"));
    EXPECT_TRUE(mock_hal::was_cls_called());
}

// ---------------------------------------------------------
// 座標の書式
//   Hu-BASIC 本来の括弧付き `(x,y)` と、括弧なしの両方を受け付ける。
//   括弧付きはマニュアル・仕様書に載っている書式だが未実装だった。
// ---------------------------------------------------------

TEST_F(GraphicsTest, PsetAcceptsParenthesizedPoint) {
    parse_and_execute(lex_tokens("PSET (100,100), 15"));

    auto cmds = mock_hal::get_draw_commands();
    ASSERT_EQ(cmds.size(), 1u) << mock_hal::get_raw_print_buffer();
    EXPECT_EQ(cmds[0].type, DrawCommand::PSET);
    EXPECT_EQ(cmds[0].x1, 100);
    EXPECT_EQ(cmds[0].y1, 100);
}

TEST_F(GraphicsTest, LineAcceptsParenthesizedPoints) {
    parse_and_execute(lex_tokens("LINE (0,0)-(100,50), 14"));

    auto cmds = mock_hal::get_draw_commands();
    ASSERT_EQ(cmds.size(), 1u) << mock_hal::get_raw_print_buffer();
    EXPECT_EQ(cmds[0].type, DrawCommand::LINE);
    EXPECT_EQ(cmds[0].x1, 0);
    EXPECT_EQ(cmds[0].y1, 0);
    EXPECT_EQ(cmds[0].x2, 100);
    EXPECT_EQ(cmds[0].y2, 50);
}

TEST_F(GraphicsTest, CircleAcceptsParenthesizedCenter) {
    parse_and_execute(lex_tokens("CIRCLE (160,120), 50, 1"));

    auto cmds = mock_hal::get_draw_commands();
    ASSERT_EQ(cmds.size(), 1u) << mock_hal::get_raw_print_buffer();
    EXPECT_EQ(cmds[0].type, DrawCommand::CIRCLE);
    EXPECT_EQ(cmds[0].x1, 160);
    EXPECT_EQ(cmds[0].y1, 120);
}

// ---------------------------------------------------------
// LINE の B / BF オプション（矩形・塗りつぶし矩形）
// ---------------------------------------------------------

TEST_F(GraphicsTest, LineWithBDrawsFourEdges) {
    parse_and_execute(lex_tokens("LINE (10,20)-(50,60), 15, B"));

    auto cmds = mock_hal::get_draw_commands();
    ASSERT_EQ(cmds.size(), 4u) << "矩形は4辺で描かれるはず: " << mock_hal::get_raw_print_buffer();

    // 4辺すべてが矩形の範囲内に収まり、辺として水平／垂直であること
    for (const auto& c : cmds) {
        EXPECT_EQ(c.type, DrawCommand::LINE);
        EXPECT_TRUE(c.x1 == c.x2 || c.y1 == c.y2) << "辺が斜めになっている";
        EXPECT_GE(std::min(c.x1, c.x2), 10);
        EXPECT_LE(std::max(c.x1, c.x2), 50);
        EXPECT_GE(std::min(c.y1, c.y2), 20);
        EXPECT_LE(std::max(c.y1, c.y2), 60);
    }
}

TEST_F(GraphicsTest, LineWithBFFillsEveryRow) {
    parse_and_execute(lex_tokens("LINE (10,20)-(50,24), 15, BF"));

    auto cmds = mock_hal::get_draw_commands();
    // y=20..24 の 5 行を横線で塗る
    ASSERT_EQ(cmds.size(), 5u) << mock_hal::get_raw_print_buffer();
    for (size_t i = 0; i < cmds.size(); i++) {
        EXPECT_EQ(cmds[i].y1, 20 + (int)i);
        EXPECT_EQ(cmds[i].y2, 20 + (int)i);
        EXPECT_EQ(cmds[i].x1, 10);
        EXPECT_EQ(cmds[i].x2, 50);
    }
}

TEST_F(GraphicsTest, LineWithoutOptionStaysASingleLine) {
    parse_and_execute(lex_tokens("LINE (0,0)-(10,10), 15"));

    auto cmds = mock_hal::get_draw_commands();
    ASSERT_EQ(cmds.size(), 1u);
    EXPECT_EQ(cmds[0].x2, 10);
    EXPECT_EQ(cmds[0].y2, 10);
}

// ---------------------------------------------------------
// POLY（多角形・星形）
// ---------------------------------------------------------

TEST_F(GraphicsTest, PolyDrawsOneLinePerVertex) {
    parse_and_execute(lex_tokens("POLY (160,120), 50, 15, 5"));

    auto cmds = mock_hal::get_draw_commands();
    EXPECT_EQ(cmds.size(), 5u) << "五角形は5辺: " << mock_hal::get_raw_print_buffer();
}

TEST_F(GraphicsTest, PolyWithSkipDrawsStar) {
    // 頂点5・skip2 は五芒星。辺の数は頂点数と同じ
    parse_and_execute(lex_tokens("POLY (160,120), 50, 15, 5, 2"));

    auto cmds = mock_hal::get_draw_commands();
    EXPECT_EQ(cmds.size(), 5u) << mock_hal::get_raw_print_buffer();

    // 星なので、隣り合う頂点ではなく離れた頂点を結ぶ＝辺が長い
    int max_len2 = 0;
    for (const auto& c : cmds) {
        int dx = c.x2 - c.x1, dy = c.y2 - c.y1;
        max_len2 = std::max(max_len2, dx * dx + dy * dy);
    }
    EXPECT_GT(max_len2, 50 * 50) << "星形の辺が短すぎる（多角形になっている）";
}

TEST_F(GraphicsTest, PolyRejectsInvalidArguments) {
    parse_and_execute(lex_tokens("POLY (160,120), 50, 15, 2"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("POLY needs 3 or more vertices"),
              std::string::npos);

    mock_hal::reset();
    parse_and_execute(lex_tokens("POLY (160,120), 50, 15, 5, 5"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("POLY skip out of range"),
              std::string::npos);
}

// ---------------------------------------------------------
// WINDOW（ユーザー座標系）
// ---------------------------------------------------------

class WindowTest : public GraphicsTest {
protected:
    void TearDown() override {
        // 座標系はグローバルなので、他のテストに持ち越さない
        reset_graphics_window();
    }
};

TEST_F(WindowTest, MapsUserCoordinatesOntoScreen) {
    // ユーザー座標 0..100 を画面 0..300 に割り当てる → 3 倍
    parse_and_execute(lex_tokens("WINDOW (0,0)-(300,200), (0,0)-(100,100)"));
    parse_and_execute(lex_tokens("PSET (50,50), 15"));

    auto cmds = mock_hal::get_draw_commands();
    ASSERT_EQ(cmds.size(), 1u) << mock_hal::get_raw_print_buffer();
    EXPECT_EQ(cmds[0].x1, 150);
    EXPECT_EQ(cmds[0].y1, 100);
}

TEST_F(WindowTest, SupportsFractionalUserCoordinates) {
    // 0.0〜1.0 の座標系。整数に丸めていないことの確認
    parse_and_execute(lex_tokens("WINDOW (0,0)-(320,240), (0,0)-(1,1)"));
    parse_and_execute(lex_tokens("PSET (0.5,0.5), 15"));

    auto cmds = mock_hal::get_draw_commands();
    ASSERT_EQ(cmds.size(), 1u) << mock_hal::get_raw_print_buffer();
    EXPECT_EQ(cmds[0].x1, 160);
    EXPECT_EQ(cmds[0].y1, 120);
}

TEST_F(WindowTest, InvertedRangeFlipsAxis) {
    // Y を反転（数学の座標系のように下が 0）
    parse_and_execute(lex_tokens("WINDOW (0,239)-(319,0), (0,0)-(319,239)"));
    parse_and_execute(lex_tokens("PSET (0,0), 15"));

    auto cmds = mock_hal::get_draw_commands();
    ASSERT_EQ(cmds.size(), 1u) << mock_hal::get_raw_print_buffer();
    EXPECT_EQ(cmds[0].x1, 0);
    EXPECT_EQ(cmds[0].y1, 239) << "Y 軸が反転していない";
}

TEST_F(WindowTest, WindowWithoutArgumentsRestoresScreenCoordinates) {
    parse_and_execute(lex_tokens("WINDOW (0,0)-(300,200), (0,0)-(100,100)"));
    parse_and_execute(lex_tokens("WINDOW"));
    parse_and_execute(lex_tokens("PSET (50,50), 15"));

    auto cmds = mock_hal::get_draw_commands();
    ASSERT_EQ(cmds.size(), 1u) << mock_hal::get_raw_print_buffer();
    EXPECT_EQ(cmds[0].x1, 50) << "WINDOW 解除後も変換が残っている";
    EXPECT_EQ(cmds[0].y1, 50);
}

TEST_F(WindowTest, AppliesToLineAndCircleToo) {
    parse_and_execute(lex_tokens("WINDOW (0,0)-(320,240), (0,0)-(160,120)")); // 2 倍
    parse_and_execute(lex_tokens("LINE (10,10)-(20,20), 15"));
    parse_and_execute(lex_tokens("CIRCLE (50,50), 10, 15"));

    auto cmds = mock_hal::get_draw_commands();
    ASSERT_EQ(cmds.size(), 2u) << mock_hal::get_raw_print_buffer();
    EXPECT_EQ(cmds[0].x1, 20);
    EXPECT_EQ(cmds[0].x2, 40);
    EXPECT_EQ(cmds[1].x1, 100);
    EXPECT_EQ(cmds[1].r, 20) << "半径がユーザー座標のまま";
}

TEST_F(WindowTest, RejectsEmptyUserRange) {
    parse_and_execute(lex_tokens("WINDOW (0,0)-(320,240), (5,0)-(5,100)"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("WINDOW user range is empty"),
              std::string::npos);
}

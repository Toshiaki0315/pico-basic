#include <gtest/gtest.h>
#include "parser.h"
#include "mock_hal_display.h"
#include "lexer.h"
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

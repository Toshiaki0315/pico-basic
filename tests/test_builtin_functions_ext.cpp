#include <gtest/gtest.h>
#include "parser.h"
#include "mock_hal_display.h"

class BuiltInFunctionsExtTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
        mock_hal::reset();
    }
};

TEST_F(BuiltInFunctionsExtTest, MathFunctions) {
    parse_and_execute(lex("PRINT SGN(-5); SGN(0); SGN(5)"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "-101\n");
    mock_hal::reset();

    parse_and_execute(lex("PRINT SQR(9)"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "3\n");
    mock_hal::reset();

    parse_and_execute(lex("PRINT INT(SIN(0)); INT(COS(0))"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "01\n");
}

TEST_F(BuiltInFunctionsExtTest, StringFunctions) {
    parse_and_execute(lex("PRINT RIGHT$(\"HELLO\", 2)"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "LO\n");
    mock_hal::reset();

    parse_and_execute(lex("PRINT CHR$(65)"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "A\n");
    mock_hal::reset();

    parse_and_execute(lex("PRINT ASC(\"ABC\")"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "65\n");
    mock_hal::reset();

    parse_and_execute(lex("PRINT VAL(\"123.45\") + 1"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "124.45\n");
    mock_hal::reset();

    parse_and_execute(lex("PRINT STR$(100) + \"$\""));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "100$\n");
}

TEST_F(BuiltInFunctionsExtTest, RndSeeding) {
    // Seeding with -1 should produce repeatable results
    parse_and_execute(lex("X = RND(-1)"));
    parse_and_execute(lex("PRINT RND(0)"));
    std::string out1 = mock_hal::get_raw_print_buffer();
    
    mock_hal::reset();
    parse_and_execute(lex("X = RND(-1)")); // reseed
    parse_and_execute(lex("PRINT RND(0)"));
    std::string out2 = mock_hal::get_raw_print_buffer();
    
    EXPECT_EQ(out1, out2);
}

#include <gtest/gtest.h>
#include "parser.h"
#include "lexer.h"
#include "mock_hal_display.h"

class ParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
        mock_hal::reset();
    }
};

TEST_F(ParserTest, EvaluateMathExpressions) {
    parse_and_execute(lex("PRINT 1 + 2 * 3"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "7\n");
    mock_hal::reset();

    parse_and_execute(lex("PRINT (1 + 2) * 3"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "9\n");
    mock_hal::reset();

    parse_and_execute(lex("PRINT 2 ^ 3")); // 8
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "8\n");
}

TEST_F(ParserTest, VariablesUsage) {
    parse_and_execute(lex("A = 10"));
    parse_and_execute(lex("LET B = 5"));
    mock_hal::reset(); // Clear 'Ok\n' outputs

    parse_and_execute(lex("PRINT A * B"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "50\n");
}

TEST_F(ParserTest, IntegerVariableUsage) {
    parse_and_execute(lex("A% = 3.99"));  // Truncates to 3
    parse_and_execute(lex("B = 3.99"));   // Floating 3.99
    mock_hal::reset();
    
    parse_and_execute(lex("PRINT A%"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "3\n");
    mock_hal::reset();
    
    parse_and_execute(lex("PRINT A% * 2")); // 6
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "6\n");
}

TEST_F(ParserTest, ExecuteSyntaxError) {
    parse_and_execute(lex("PRINT ="));
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().find("Syntax Error") != std::string::npos);
}

// ---------------------------------------------------------
// Execution Tests (Control Flow)
// ---------------------------------------------------------
class ExecutionTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
        mock_hal::reset();
    }
};

TEST_F(ExecutionTest, SequentialExecution) {
    store_line(10, lex("A = 5"));
    store_line(20, lex("B = 10"));
    store_line(30, lex("PRINT A + B"));
    run_program();
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "15\n");
}

TEST_F(ExecutionTest, GotoStatement) {
    store_line(10, lex("A = 1"));
    store_line(20, lex("GOTO 40"));
    store_line(30, lex("A = 2"));
    store_line(40, lex("PRINT A"));
    run_program(10);
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "1\n");
}

TEST_F(ExecutionTest, IfStatementTrue) {
    store_line(10, lex("A = 10"));
    store_line(20, lex("IF A > 5 THEN PRINT A"));
    store_line(30, lex("IF A = 5 THEN PRINT A + 1"));
    run_program(10);
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "10\n");
}

TEST_F(ExecutionTest, IfKeywordBranching) {
    store_line(10, lex("A = 1"));
    store_line(20, lex("IF A = 1 THEN GOTO 40")); // If True => Jump to 40
    store_line(30, lex("PRINT 999")); // Should be skipped
    store_line(40, lex("PRINT A")); 
    run_program(10);
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "1\n");
}

TEST_F(ExecutionTest, IfElseBranching) {
    store_line(10, lex("A = 0"));
    store_line(20, lex("IF A = 1 THEN PRINT 111 ELSE PRINT 222"));
    store_line(30, lex("IF A = 0 THEN PRINT 333 ELSE PRINT 444"));
    run_program(10);
    // A=0, so 111 is skipped, 222 is printed.
    // A=0, so 333 is printed, 444 is skipped.
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "222\n333\n");
}

TEST_F(ExecutionTest, ForNextLoop) {
    store_line(10, lex("FOR I = 1 TO 3"));
    store_line(20, lex("PRINT I"));
    store_line(30, lex("NEXT I"));
    run_program(50);
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "1\n2\n3\n");
}

TEST_F(ExecutionTest, ForNextWithStep) {
    store_line(10, lex("FOR I = 10 TO 0 STEP -5"));
    store_line(20, lex("PRINT I"));
    store_line(30, lex("NEXT I"));
    run_program(50);
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "10\n5\n0\n");
}

TEST_F(ExecutionTest, SyntaxErrorInDeferred) {
    store_line(10, lex("PRINT (1+2")); 
    run_program(10);
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().find("Error 2 in line 10") != std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

TEST_F(ExecutionTest, ForNextNested) {
    store_line(10, lex("FOR I = 1 TO 2"));
    store_line(20, lex("FOR J = 1 TO 2"));
    store_line(30, lex("PRINT I * J"));
    store_line(40, lex("NEXT J"));
    store_line(50, lex("NEXT I"));
    run_program(50);
    // Expected values: I=1,J=1 (1), I=1,J=2 (2), I=2,J=1 (2), I=2,J=2 (4)
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "1\n2\n2\n4\n");
}

TEST_F(ExecutionTest, GosubReturn) {
    store_line(10, lex("GOSUB 30"));
    store_line(20, lex("GOTO 50"));
    store_line(30, lex("PRINT 111"));
    store_line(40, lex("RETURN"));
    store_line(50, lex("PRINT 222"));
    run_program(20);
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "111\n222\n");
}

TEST_F(ExecutionTest, GosubNested) {
    store_line(100, lex("GOSUB 200")); // Calls 200
    store_line(110, lex("PRINT 1"));
    store_line(120, lex("GOTO 400"));  // Ends
    
    store_line(200, lex("GOSUB 300")); // Calls 300
    store_line(210, lex("PRINT 2"));
    store_line(220, lex("RETURN"));    // Returns to 110

    store_line(300, lex("PRINT 3"));
    store_line(310, lex("RETURN"));    // Returns to 210

    store_line(400, lex("PRINT 4"));

    run_program(20);
    // Order: 3 -> 2 -> 1 -> 4
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "3\n2\n1\n4\n");
}

TEST_F(ExecutionTest, ReturnWithoutGosub) {
    store_line(10, lex("RETURN"));
    run_program(5);
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().find("Error in line 10: RETURN WITHOUT GOSUB") != std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

TEST_F(ExecutionTest, GosubInsideForLoop) {
    // ループの各回でサブルーチンを呼ぶ。呼び出しごとに正しく復帰する
    store_line(10, lex("FOR I=1 TO 3"));
    store_line(20, lex("GOSUB 100"));
    store_line(30, lex("NEXT I"));
    store_line(40, lex("END"));
    store_line(100, lex("PRINT I"));
    store_line(110, lex("RETURN"));
    run_program(50);
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "1\n2\n3\n");
}

TEST_F(ExecutionTest, OnGosubDispatch) {
    store_line(10, lex("K=2 : ON K GOSUB 100, 200, 300"));
    store_line(20, lex("PRINT \"BACK\" : END"));
    store_line(100, lex("PRINT \"ONE\" : RETURN"));
    store_line(200, lex("PRINT \"TWO\" : RETURN"));
    store_line(300, lex("PRINT \"THREE\" : RETURN"));
    run_program(50);
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "TWO\nBACK\n");
}

TEST_F(ExecutionTest, GosubSubroutineComputesResult) {
    // 変数で引数を渡し、結果を変数で受け取るサブルーチン（4 の階乗）
    store_line(10, lex("N=4 : GOSUB 100"));
    store_line(20, lex("PRINT F"));
    store_line(30, lex("END"));
    store_line(100, lex("F=1"));
    store_line(110, lex("FOR J=1 TO N"));
    store_line(120, lex("F=F*J"));
    store_line(130, lex("NEXT J"));
    store_line(140, lex("RETURN"));
    run_program(200);
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "24\n");
}

TEST_F(ExecutionTest, GosubDeepNesting) {
    // 4 段のネストが LIFO で正しく戻る。
    // GOSUB は行番号単位で復帰するため、各サブルーチンは行を分けて書く
    // （`GOSUB … : 文` のように同じ行に続けた文は復帰時に実行されない）
    store_line(10, lex("GOSUB 100"));
    store_line(20, lex("PRINT 1"));
    store_line(30, lex("END"));
    store_line(100, lex("GOSUB 200"));
    store_line(110, lex("PRINT 2"));
    store_line(120, lex("RETURN"));
    store_line(200, lex("GOSUB 300"));
    store_line(210, lex("PRINT 3"));
    store_line(220, lex("RETURN"));
    store_line(300, lex("GOSUB 400"));
    store_line(310, lex("PRINT 4"));
    store_line(320, lex("RETURN"));
    store_line(400, lex("PRINT 5"));
    store_line(410, lex("RETURN"));
    run_program(50);
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "5\n4\n3\n2\n1\n");
}

// ---------------------------------------------------------
// System Commands Test
// ---------------------------------------------------------
class SystemCommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
        mock_hal::reset();
    }
};

TEST_F(SystemCommandTest, LineInsertionAndDeletion) {
    parse_and_execute(lex("10 A = 5"));
    parse_and_execute(lex("20 PRINT A"));
    parse_and_execute(lex("RUN"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "5\n");
    mock_hal::reset();

    parse_and_execute(lex("10 A = 10"));
    parse_and_execute(lex("RUN"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "10\n");
    mock_hal::reset();

    parse_and_execute(lex("10"));
    parse_and_execute(lex("NEW"));
    parse_and_execute(lex("20 PRINT A"));
    parse_and_execute(lex("RUN"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "0\n"); 
}

TEST_F(SystemCommandTest, ListProgram) {
    parse_and_execute(lex("10 FOR I = 1 TO 3"));
    parse_and_execute(lex("20 PRINT I"));
    parse_and_execute(lex("30 NEXT I"));
    parse_and_execute(lex("LIST"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "10 FOR I = 1 TO 3\n20 PRINT I\n30 NEXT I\n");
}

TEST_F(SystemCommandTest, NewCommand) {
    parse_and_execute(lex("10 PRINT 123"));
    parse_and_execute(lex("NEW"));
    parse_and_execute(lex("RUN")); 
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "");
}

// ---------------------------------------------------------
// Strings and Arrays Test
// ---------------------------------------------------------
class StringArrayTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
        mock_hal::reset();
    }
};

TEST_F(StringArrayTest, StringAssignmentAndConcatenation) {
    parse_and_execute(lex("A$ = \"HELLO\""));
    parse_and_execute(lex("B$ = \" WORLD\""));
    mock_hal::reset();
    
    parse_and_execute(lex("PRINT A$ + B$"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "HELLO WORLD\n");
}

TEST_F(StringArrayTest, NumberArrayUsage) {
    parse_and_execute(lex("DIM A(5)"));
    parse_and_execute(lex("A(0) = 111"));
    parse_and_execute(lex("A(5) = 999"));
    mock_hal::reset();
    
    parse_and_execute(lex("PRINT A(0)"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "111\n");
    mock_hal::reset();
    
    parse_and_execute(lex("PRINT A(5)"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "999\n");
}

TEST_F(StringArrayTest, StringArrayUsage) {
    parse_and_execute(lex("DIM Z$(2)"));
    parse_and_execute(lex("Z$(1) = \"TEST\""));
    mock_hal::reset();
    
    parse_and_execute(lex("PRINT Z$(1)"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "TEST\n");
}

TEST_F(StringArrayTest, OutOfBoundsCheck) {
    parse_and_execute(lex("DIM B(3)"));
    mock_hal::reset();
    parse_and_execute(lex("B(4) = 100"));
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().find("out of bounds") != std::string::npos);
}

TEST_F(StringArrayTest, TypeMismatchCheck) {
    parse_and_execute(lex("A$ = 123"));
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().find("Type Mismatch") != std::string::npos);
    mock_hal::reset();
    
    parse_and_execute(lex("A = \"FOO\""));
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().find("Type Mismatch") != std::string::npos);
    mock_hal::reset();
    
    parse_and_execute(lex("PRINT 1 + \"STR\""));
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().find("Type Mismatch") != std::string::npos);
}

// ---------------------------------------------------------
// Data and Read Test
// ---------------------------------------------------------
class DataReadTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
        mock_hal::reset();
    }
};

TEST_F(DataReadTest, BasicReadData) {
    store_line(10, lex("READ A, B, C$"));
    store_line(20, lex("PRINT A"));
    store_line(30, lex("PRINT B"));
    store_line(40, lex("PRINT C$"));
    store_line(50, lex("DATA 10, -20, \"HELLO\""));
    run_program();
    
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "10\n-20\nHELLO\n");
}

TEST_F(DataReadTest, RestorePointer) {
    store_line(10, lex("DATA 50, 100"));
    store_line(20, lex("READ X"));
    store_line(30, lex("RESTORE"));
    store_line(40, lex("READ Y, Z"));
    store_line(50, lex("PRINT X"));
    store_line(60, lex("PRINT Y"));
    store_line(70, lex("PRINT Z"));
    run_program();
    
    // X gets 50, then RESTORE resets pointer.
    // Y gets 50, Z gets 100.
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "50\n50\n100\n");
}

TEST_F(DataReadTest, OutOfDataError) {
    store_line(10, lex("DATA 1"));
    store_line(20, lex("READ A, B")); // Needs 2 values, only 1 provided
    run_program();
    
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().find("Out of DATA") != std::string::npos);
}

// ---------------------------------------------------------
// Built-in Functions Test
// ---------------------------------------------------------
class BuiltInFunctionsTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
        mock_hal::reset();
    }
};

TEST_F(BuiltInFunctionsTest, MathFunctions) {
    parse_and_execute(lex("PRINT ABS(-15)"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "15\n");
    mock_hal::reset();
    
    parse_and_execute(lex("PRINT INT(3.7)"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "3\n");
    mock_hal::reset();
    
    parse_and_execute(lex("A = RND(10)")); // RND between 0 and 10
    // Result should be > 0 (well, >= 0) but won't crash
    // Since we don't know the exact value, verify error wasn't encountered
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "");
}

TEST_F(BuiltInFunctionsTest, StringFunctions) {
    parse_and_execute(lex("A$ = \"HELLO WORLD\""));
    
    parse_and_execute(lex("PRINT LEN(A$)"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "11\n");
    mock_hal::reset();
    
    parse_and_execute(lex("PRINT LEFT$(A$, 4)"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "HELL\n");
    mock_hal::reset();
    
    // MID$(STRING, start(1-based), length)
    parse_and_execute(lex("PRINT MID$(A$, 7, 5)"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "WORLD\n");
    mock_hal::reset();
}

TEST_F(BuiltInFunctionsTest, FunctionErrorHandling) {
    parse_and_execute(lex("PRINT ABS(\"STRING\")"));
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().find("Type Mismatch") != std::string::npos);
    mock_hal::reset();
    
    parse_and_execute(lex("PRINT LEN(123)"));
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().find("Type Mismatch") != std::string::npos);
}

// ---------------------------------------------------------
// Phase 2: Extended Keywords Stubs
// ---------------------------------------------------------
class Phase2ExtensionTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
        mock_hal::reset();
    }
};

TEST_F(Phase2ExtensionTest, InitNewonAreNoOps) {
    // INIT / NEWON は互換のための空実装。引数があってもエラーや出力を出さず、
    // 同じ行の後続文（`:` 以降）は通常どおり実行される。
    parse_and_execute(lex("INIT"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "");
    mock_hal::reset();
    parse_and_execute(lex("NEWON &H8000"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "");
    mock_hal::reset();
    parse_and_execute(lex("INIT : PRINT 42"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "42\n");
    mock_hal::reset();
}


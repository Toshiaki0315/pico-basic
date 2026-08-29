#include <gtest/gtest.h>
#include "parser.h"
#include "lexer.h"
#include "mock_hal_display.h"
#include "hal_display.h"
#include <cstdio>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstring>

class FileOpsTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
        mock_hal::reset();
        std::remove("test_save.bas");
    }
    void TearDown() override {
        std::remove("test_save.bas");
    }
};

TEST_F(FileOpsTest, SaveAndLoadProgram) {
    // 1. Create a program
    store_line(10, lex("A = 10"));
    store_line(20, lex("PRINT A * 2"));
    
    // 2. Save it with 0: prefix
    parse_and_execute(lex("SAVE \"0:test_save.bas\""));
    mock_hal::reset();
    
    // 3. Clear program
    parse_and_execute(lex("NEW"));
    
    // 4. Load it with CAS: prefix
    parse_and_execute(lex("LOAD \"CAS:test_save.bas\""));
    mock_hal::reset();
    
    // 5. Run it and verify output
    parse_and_execute(lex("RUN"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "20\n");
}

TEST_F(FileOpsTest, LoadNonExistentFile) {
    parse_and_execute(lex("LOAD \"missing.bas\""));
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().find("File Error") != std::string::npos);
}

TEST_F(FileOpsTest, FilesCommand) {
    // Just verify it runs and prints something
    parse_and_execute(lex("FILES"));
    std::string out = mock_hal::get_raw_print_buffer();
    EXPECT_TRUE(out.find("File(s) found") != std::string::npos);
}

TEST_F(FileOpsTest, KillCommand) {
    // 1. Create a dummy file
    {
        std::ofstream fs("kill_test.tmp");
        fs << "test";
    }
    
    // 2. Kill it
    parse_and_execute(lex("KILL \"kill_test.tmp\""));
    
    // 3. Verify it's gone
    std::ifstream fs("kill_test.tmp");
    EXPECT_FALSE(fs.good());
    EXPECT_TRUE(mock_hal::get_raw_print_buffer().find("Deleted") != std::string::npos);
}

TEST_F(FileOpsTest, NameAsCommand) {
    // 1. Create a dummy file
    {
        std::ofstream fs("name_test_old.tmp");
        fs << "test";
    }
    std::remove("name_test_new.tmp");
    
    // 2. Rename it
    parse_and_execute(lex("NAME \"name_test_old.tmp\" AS \"name_test_new.tmp\""));
    
    // 3. Verify
    std::ifstream fs_old("name_test_old.tmp");
    EXPECT_FALSE(fs_old.good());
    std::ifstream fs_new("name_test_new.tmp");
    EXPECT_TRUE(fs_new.good());
    
    std::remove("name_test_new.tmp");
}


// ---------------------------------------------------------
// 出力先の回帰テスト
//
// BASIC の出力は LCD（mock_hal のバッファ）とシリアル端末（stdout）の
// 両方に出す必要がある。ファイル系コマンドが LCD にしか出さない
// 不具合があったため、端末側にも出ることを固定する。
// ---------------------------------------------------------

static std::string capture_stdout(const char* source) {
    testing::internal::CaptureStdout();
    parse_and_execute(lex(source));
    return testing::internal::GetCapturedStdout();
}

TEST_F(FileOpsTest, FilesPrintsToTerminalAndDisplay) {
    std::string out = capture_stdout("FILES");

    EXPECT_NE(out.find("File(s) found"), std::string::npos)
        << "FILES の結果が端末に出ていない: " << out;
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("File(s) found"), std::string::npos)
        << "FILES の結果が LCD に出ていない";
}

TEST_F(FileOpsTest, SavePrintsToTerminalAndDisplay) {
    store_line(10, lex("A = 1"));

    std::string out = capture_stdout("SAVE \"save_out_test.tmp\"");
    std::remove("save_out_test.tmp");

    EXPECT_NE(out.find("Saved"), std::string::npos)
        << "SAVE の結果が端末に出ていない: " << out;
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("Saved"), std::string::npos)
        << "SAVE の結果が LCD に出ていない";
}

// ---------------------------------------------------------
// FILES の一覧規則
//   - ディレクトリと隠しファイルは出さない（SD 上の
//     "System Volume Information" 等を混ぜないため）
//   - 名前が桁幅に収まらない場合も、隣の名前と繋がらない
// ---------------------------------------------------------

class FilesListingTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
        mock_hal::reset();
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_ / "System Volume Information");
        std::ofstream(dir_ / "TEST.BAS") << "10 END\n";
        std::ofstream(dir_ / ".hidden") << "x";
        prev_ = std::filesystem::current_path();
        std::filesystem::current_path(dir_);
    }
    void TearDown() override {
        std::filesystem::current_path(prev_);
        std::filesystem::remove_all(dir_);
    }
    std::filesystem::path dir_ = std::filesystem::temp_directory_path() / "pico_basic_files_test";
    std::filesystem::path prev_;
};

TEST_F(FilesListingTest, SkipsDirectoriesAndHiddenEntries) {
    parse_and_execute(lex("FILES"));
    std::string out = mock_hal::get_raw_print_buffer();

    EXPECT_EQ(out.find("System Volume Information"), std::string::npos)
        << "ディレクトリが一覧に出ている: " << out;
    EXPECT_EQ(out.find(".hidden"), std::string::npos)
        << "隠しファイルが一覧に出ている: " << out;
    EXPECT_NE(out.find("TEST.BAS"), std::string::npos) << out;
    EXPECT_NE(out.find("1 File(s) found"), std::string::npos)
        << "件数がファイル数と一致しない: " << out;
}

TEST_F(FilesListingTest, LongNameDoesNotRunIntoNextName) {
    std::ofstream(dir_ / "VERYLONGFILENAME.BAS") << "10 END\n";
    mock_hal::reset();

    parse_and_execute(lex("FILES"));
    std::string out = mock_hal::get_raw_print_buffer();

    // 長い名前の直後は必ず改行。隣の名前と連結しない
    size_t p = out.find("VERYLONGFILENAME.BAS");
    ASSERT_NE(p, std::string::npos) << out;
    EXPECT_EQ(out[p + strlen("VERYLONGFILENAME.BAS")], '\n')
        << "長いファイル名の後で改行されていない: " << out;
}

// ---------------------------------------------------------
// シーケンシャルファイル I/O（OPEN / PRINT# / INPUT# / EOF / CLOSE）
// ---------------------------------------------------------
class SeqFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        clear_program();
        mock_hal::reset();
        std::remove("T_SEQ.DAT");
    }
    void TearDown() override {
        parse_and_execute(lex("CLOSE"));
        std::remove("T_SEQ.DAT");
    }
    void run(const char* line) { parse_and_execute(lex(line)); }
};

TEST_F(SeqFileTest, WriteAndReadBack) {
    store_line(10, lex("OPEN \"T_SEQ.DAT\" FOR OUTPUT AS #1"));
    store_line(20, lex("PRINT #1, 100"));
    store_line(30, lex("PRINT #1, \"ALICE\", 250"));
    store_line(40, lex("CLOSE #1"));
    store_line(50, lex("OPEN \"T_SEQ.DAT\" FOR INPUT AS #1"));
    store_line(60, lex("INPUT #1, A"));
    store_line(70, lex("INPUT #1, N$, B"));
    store_line(80, lex("CLOSE #1"));
    store_line(90, lex("PRINT A; N$; B"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "100ALICE250\n");
}

TEST_F(SeqFileTest, AppendAndEofLoop) {
    store_line(10, lex("OPEN \"T_SEQ.DAT\" FOR OUTPUT AS #1"));
    store_line(20, lex("PRINT #1, 1"));
    store_line(30, lex("CLOSE #1"));
    store_line(40, lex("OPEN \"T_SEQ.DAT\" FOR APPEND AS #1"));
    store_line(50, lex("PRINT #1, 2"));
    store_line(60, lex("CLOSE #1"));
    store_line(70, lex("OPEN \"T_SEQ.DAT\" FOR INPUT AS #1"));
    store_line(80, lex("REPEAT"));
    store_line(90, lex("INPUT #1, X"));
    store_line(100, lex("PRINT X"));
    store_line(110, lex("UNTIL EOF(1) = 1"));
    store_line(120, lex("CLOSE #1"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "1\n2\n");
}

TEST_F(SeqFileTest, HiscoreFirstRunIdiom) {
    // APPEND で存在を保証してから EOF ガード付きで読む定石
    store_line(10, lex("OPEN \"T_SEQ.DAT\" FOR APPEND AS #1 : CLOSE #1"));
    store_line(20, lex("OPEN \"T_SEQ.DAT\" FOR INPUT AS #1"));
    store_line(30, lex("HS = 0 : IF EOF(1) = 0 THEN INPUT #1, HS"));
    store_line(40, lex("CLOSE #1"));
    store_line(50, lex("PRINT HS"));
    run("RUN");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "0\n"); // 初回はファイルが空
}

TEST_F(SeqFileTest, InputPastEndErrors) {
    store_line(10, lex("OPEN \"T_SEQ.DAT\" FOR OUTPUT AS #1 : CLOSE #1"));
    store_line(20, lex("OPEN \"T_SEQ.DAT\" FOR INPUT AS #1"));
    store_line(30, lex("INPUT #1, A"));
    run("RUN");
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("Input past end"), std::string::npos);
}

TEST_F(SeqFileTest, FileNotOpenErrors) {
    mock_hal::reset();
    parse_and_execute(lex("INPUT #3, A"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("File not open"), std::string::npos);
}

TEST_F(SeqFileTest, BadModeErrors) {
    store_line(10, lex("OPEN \"T_SEQ.DAT\" FOR OUTPUT AS #1"));
    store_line(20, lex("INPUT #1, A"));
    run("RUN");
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("Bad file mode"), std::string::npos);
}

TEST_F(SeqFileTest, MissingFileErrors) {
    mock_hal::reset();
    parse_and_execute(lex("OPEN \"NO_SUCH.XYZ\" FOR INPUT AS #1"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("File not found"), std::string::npos);
}

TEST_F(SeqFileTest, DoubleSigilVariableStillWorks) {
    // '#' を識別子に許した副作用の確認: 倍精度シジル付き変数が使える
    run("PI# = 3.5");
    mock_hal::reset();
    run("PRINT PI#");
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "3.5\n");
}

// ---------------------------------------------------------
// スクリーンショット（Ctrl-P で保存する BMP）
// 画素取得もファイル出力も HAL 経由なので、ホストで中身まで検証できる
// ---------------------------------------------------------
#include "screenshot.h"
#include "parser_internal.h"   // PALETTE

class ScreenshotTest : public ::testing::Test {
protected:
    void SetUp() override { mock_hal::reset(); std::remove("SHOT.BMP"); }
    void TearDown() override { std::remove("SHOT.BMP"); }

    static std::vector<unsigned char> read_file(const char* p) {
        std::ifstream f(p, std::ios::binary);
        return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),
                                           std::istreambuf_iterator<char>());
    }
    static uint32_t le32(const std::vector<unsigned char>& b, size_t o) {
        return (uint32_t)b[o] | ((uint32_t)b[o+1] << 8) |
               ((uint32_t)b[o+2] << 16) | ((uint32_t)b[o+3] << 24);
    }
};

TEST_F(ScreenshotTest, WritesValidBmpHeader) {
    ASSERT_TRUE(screenshot_save("SHOT.BMP"));
    auto b = read_file("SHOT.BMP");
    ASSERT_GE(b.size(), 54u);
    EXPECT_EQ(b[0], 'B');
    EXPECT_EQ(b[1], 'M');
    EXPECT_EQ(le32(b, 10), 54u);       // 画素データの開始位置
    EXPECT_EQ(le32(b, 14), 40u);       // BITMAPINFOHEADER
    EXPECT_EQ(le32(b, 18), 320u);      // 幅
    EXPECT_EQ(le32(b, 22), 240u);      // 高さ
    EXPECT_EQ(b[28], 24);              // 24bit
    EXPECT_EQ(le32(b, 30), 0u);        // 無圧縮
    // 54 + 320*3*240 とファイルサイズが一致する
    EXPECT_EQ(b.size(), 54u + 320u * 3u * 240u);
    EXPECT_EQ(le32(b, 2), (uint32_t)b.size());
}

TEST_F(ScreenshotTest, PixelsRoundTripAsBgr) {
    // 赤（パレット 4）を 1 点打ち、BMP の該当位置が BGR=00,00,FF になること
    parse_and_execute(lex("PSET (5,5), 4"));
    ASSERT_TRUE(screenshot_save("SHOT.BMP"));
    auto b = read_file("SHOT.BMP");

    const int stride = 320 * 3;
    // BMP は下から上に並ぶので、画面の y 行はファイル上の (239-y) 行目
    size_t off = 54 + (size_t)(239 - 5) * stride + 5 * 3;
    ASSERT_LT(off + 2, b.size());
    EXPECT_EQ(b[off + 0], 0x00) << "B";
    EXPECT_EQ(b[off + 1], 0x00) << "G";
    EXPECT_EQ(b[off + 2], 0xFF) << "R";

    // 打っていない画素は黒
    size_t off2 = 54 + (size_t)(239 - 6) * stride + 6 * 3;
    EXPECT_EQ(b[off2 + 0], 0x00);
    EXPECT_EQ(b[off2 + 1], 0x00);
    EXPECT_EQ(b[off2 + 2], 0x00);
}

TEST_F(ScreenshotTest, WhiteAndBlackAreExact) {
    // RGB565→888 で白が 0xFFFFFF に戻る（上位ビットの複製が効いている）
    parse_and_execute(lex("PSET (0,0), 15"));  // 白
    ASSERT_TRUE(screenshot_save("SHOT.BMP"));
    auto b = read_file("SHOT.BMP");
    size_t off = 54 + (size_t)(239 - 0) * (320 * 3) + 0;
    EXPECT_EQ(b[off + 0], 0xFF);
    EXPECT_EQ(b[off + 1], 0xFF);
    EXPECT_EQ(b[off + 2], 0xFF);
}

TEST_F(ScreenshotTest, AutoNumbersFilenames) {
    std::remove("SCR00.BMP"); std::remove("SCR01.BMP");
    char n1[16] = "", n2[16] = "";
    ASSERT_TRUE(screenshot_save_next(n1, sizeof(n1)));
    EXPECT_STREQ(n1, "SCR00.BMP");
    ASSERT_TRUE(screenshot_save_next(n2, sizeof(n2)));
    EXPECT_STREQ(n2, "SCR01.BMP") << "既存ファイルを上書きしてしまう";
    std::remove("SCR00.BMP"); std::remove("SCR01.BMP");
}

// --- LINE INPUT #（カンマを含む文字列の往復）-----------------------------

TEST_F(FileOpsTest, LineInputKeepsCommasIntact) {
    // INPUT # ではカンマで割れてしまう文字列が、そのまま復元できる
    store_line(10, lex("OPEN \"CSV.DAT\" FOR OUTPUT AS #1"));
    store_line(20, lex("PRINT #1, \"TOKYO, JAPAN\""));
    store_line(30, lex("CLOSE #1"));
    store_line(40, lex("OPEN \"CSV.DAT\" FOR INPUT AS #1"));
    store_line(50, lex("LINE INPUT #1, A$"));
    store_line(60, lex("CLOSE #1"));
    store_line(70, lex("PRINT A$"));
    parse_and_execute(lex("RUN"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "TOKYO, JAPAN\n");
}

TEST_F(FileOpsTest, PlainInputStillSplitsOnCommas) {
    // 従来の INPUT # の挙動は変わっていないこと（比較用）
    store_line(10, lex("OPEN \"CSV2.DAT\" FOR OUTPUT AS #1"));
    store_line(20, lex("PRINT #1, \"TOKYO, JAPAN\""));
    store_line(30, lex("CLOSE #1"));
    store_line(40, lex("OPEN \"CSV2.DAT\" FOR INPUT AS #1"));
    store_line(50, lex("INPUT #1, A$"));
    store_line(60, lex("CLOSE #1"));
    store_line(70, lex("PRINT A$"));
    parse_and_execute(lex("RUN"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "TOKYO\n"); // カンマ以降が切れる
}

TEST_F(FileOpsTest, LineInputReadsSuccessiveLines) {
    store_line(10, lex("OPEN \"L.DAT\" FOR OUTPUT AS #1"));
    store_line(20, lex("PRINT #1, \"FIRST, LINE\""));
    store_line(30, lex("PRINT #1, \"SECOND, LINE\""));
    store_line(40, lex("CLOSE #1"));
    store_line(50, lex("OPEN \"L.DAT\" FOR INPUT AS #1"));
    store_line(60, lex("LINE INPUT #1, A$"));
    store_line(70, lex("LINE INPUT #1, B$"));
    store_line(80, lex("CLOSE #1"));
    store_line(90, lex("PRINT A$; \"/\"; B$"));
    parse_and_execute(lex("RUN"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "FIRST, LINE/SECOND, LINE\n");
}

TEST_F(FileOpsTest, LineInputWorksWithEofLoop) {
    store_line(10, lex("OPEN \"L2.DAT\" FOR OUTPUT AS #1"));
    store_line(20, lex("PRINT #1, \"A, B\""));
    store_line(30, lex("PRINT #1, \"C, D\""));
    store_line(40, lex("CLOSE #1"));
    store_line(50, lex("OPEN \"L2.DAT\" FOR INPUT AS #1"));
    store_line(60, lex("IF EOF(1) THEN GOTO 100"));
    store_line(70, lex("LINE INPUT #1, A$"));
    store_line(80, lex("PRINT A$"));
    store_line(90, lex("GOTO 60"));
    store_line(100, lex("CLOSE #1"));
    parse_and_execute(lex("RUN"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "A, B\nC, D\n");
}

TEST_F(FileOpsTest, LineInputRejectsNumericVariable) {
    store_line(10, lex("OPEN \"L3.DAT\" FOR OUTPUT AS #1"));
    store_line(20, lex("PRINT #1, \"X\""));
    store_line(30, lex("CLOSE #1"));
    store_line(40, lex("OPEN \"L3.DAT\" FOR INPUT AS #1"));
    store_line(50, lex("LINE INPUT #1, N"));
    parse_and_execute(lex("RUN"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("string variable"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
    parse_and_execute(lex("CLOSE"));
}

TEST_F(FileOpsTest, GraphicsLineStillWorks) {
    // `LINE INPUT` を足しても図形の LINE が壊れていないこと
    mock_hal::reset();
    parse_and_execute(lex("LINE (0,0)-(50,50), 15"));
    EXPECT_FALSE(mock_hal::get_draw_commands().empty());
}

// --- SAVE / LOAD の往復 --------------------------------------------------
//
// SAVE は LIST とは別に書き戻しを組み立てていて、REM の分岐が抜けていた。
// REM トークンの text はコメント本文だけで命令語を含まないため、保存すると
// コメントが裸の式として書き出され、読み戻すと別のプログラムになっていた。

TEST_F(FileOpsTest, SaveKeepsRemComments) {
    store_line(10, lex("REM --- TITLE ---"));
    store_line(20, lex("PRINT 1"));
    parse_and_execute(lex("SAVE \"RT.DAT\""));

    clear_program();
    mock_hal::reset();
    parse_and_execute(lex("LOAD \"RT.DAT\""));
    parse_and_execute(lex("LIST"));
    std::string out = mock_hal::get_raw_print_buffer();
    EXPECT_NE(out.find("10 REM --- TITLE ---"), std::string::npos) << out;
}

// `'` は字句解析の時点で REM と同じものになるので、書き戻しは REM に揃う
TEST_F(FileOpsTest, SaveKeepsApostropheComments) {
    store_line(10, lex("' NOTE"));
    parse_and_execute(lex("SAVE \"RT2.DAT\""));

    clear_program();
    mock_hal::reset();
    parse_and_execute(lex("LOAD \"RT2.DAT\""));
    parse_and_execute(lex("LIST"));
    EXPECT_NE(mock_hal::get_raw_print_buffer().find("10 REM NOTE"), std::string::npos)
        << mock_hal::get_raw_print_buffer();
}

// 行末のコメントも同じ。ここが壊れていると、保存したプログラムの実行結果まで変わる
TEST_F(FileOpsTest, SaveKeepsTrailingComment) {
    store_line(10, lex("A = 5 : REM SET A"));
    store_line(20, lex("PRINT A"));
    parse_and_execute(lex("SAVE \"RT3.DAT\""));

    clear_program();
    mock_hal::reset();
    parse_and_execute(lex("LOAD \"RT3.DAT\""));
    mock_hal::reset(); // LOAD の "Loaded" を数えない
    parse_and_execute(lex("RUN"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), "5\n");
}

// 往復して LIST が一致すること。SAVE と LIST は同じ書き戻しを使う約束なので、
// 新しいトークン種別が増えても片方だけ抜けたらここで落ちる
TEST_F(FileOpsTest, SaveLoadRoundTripMatchesList) {
    store_line(10, lex("REM --- HEADER ---"));
    store_line(20, lex("DIM A(3)"));
    store_line(30, lex("S$ = \"0111\""));
    store_line(40, lex("FOR I = 0 TO 3 : A(I) = VAL(MID$(S$,I+1,1)) : NEXT I"));
    store_line(50, lex("IF A(1) <> 0 THEN PRINT \"OK\" : REM TRAILING"));
    store_line(60, lex("*LOOP"));
    store_line(70, lex("GOSUB *LOOP"));

    mock_hal::reset();
    parse_and_execute(lex("LIST"));
    std::string before = mock_hal::get_raw_print_buffer();

    parse_and_execute(lex("SAVE \"RT4.DAT\""));
    clear_program();
    parse_and_execute(lex("LOAD \"RT4.DAT\""));

    mock_hal::reset();
    parse_and_execute(lex("LIST"));
    EXPECT_EQ(mock_hal::get_raw_print_buffer(), before);
}

// 保存と読み込みを繰り返しても増えない・減らないこと。
// SAVE がトークンの後ろに空白を置き、LOAD が行末の改行を落としていなかった
// ころは、REM の本文が往復のたびに 1 文字ずつ伸びていた
TEST_F(FileOpsTest, RepeatedSaveLoadIsStable) {
    store_line(10, lex("REM NOTE"));
    store_line(20, lex("A = 1 : REM TAIL"));

    std::string prev;
    for (int cycle = 0; cycle < 3; cycle++) {
        parse_and_execute(lex("SAVE \"RT5.DAT\""));
        clear_program();
        parse_and_execute(lex("LOAD \"RT5.DAT\""));

        mock_hal::reset();
        parse_and_execute(lex("LIST"));
        std::string now = mock_hal::get_raw_print_buffer();
        if (cycle > 0) EXPECT_EQ(now, prev) << "往復 " << cycle << " 回目で変化した";
        prev = now;
    }
    EXPECT_NE(prev.find("10 REM NOTE\n"), std::string::npos) << prev;
    EXPECT_NE(prev.find("20 A = 1 : REM TAIL\n"), std::string::npos) << prev;
}

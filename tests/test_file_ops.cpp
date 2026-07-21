#include <gtest/gtest.h>
#include "parser.h"
#include "mock_hal_display.h"
#include <cstdio>
#include <fstream>
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

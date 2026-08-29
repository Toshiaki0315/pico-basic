#include "parser_internal.h"
#include "hal_display.h"
#include "hal_sdcard.h"
#include "line_input.h"
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cctype>

// ファイルを扱う文。
// シーケンシャル I/O（OPEN / CLOSE / PRINT# / INPUT# / LINE INPUT# / EOF）と、
// SD カード上のファイル操作（SAVE / LOAD / KILL / NAME / FILES）。

// ---------------------------------------------------------
// シーケンシャルファイル I/O（OPEN / PRINT# / INPUT# / EOF / CLOSE）
//
// PRINT # は値を "," 区切りの 1 行として書き、INPUT # はカンマまたは
// 行末で区切られたフィールドを読む。この対で往復できる。
// ---------------------------------------------------------
struct BasicFile {
    void* fp;
    int   mode;          // 0=未使用 1=INPUT 2=OUTPUT/APPEND
    char  linebuf[160];  // INPUT# 用の行バッファ
    int   linepos;
    bool  line_valid;
};
static BasicFile basic_files[MAX_BASIC_FILES + 1]; // 添字 1〜4 を使う

static BasicFile& file_slot(int n) {
    if (n < 1 || n > MAX_BASIC_FILES)
        throw std::runtime_error("Illegal function call: file number must be 1-4");
    return basic_files[n];
}

// `#n` を読む（# は省略可）
static int parse_file_number(const TokenList& tokens, int& pos) {
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::HASH) pos++;
    Value v = parse_relation(tokens, pos);
    if (!v.is_numeric()) throw std::runtime_error("Type Mismatch: file number");
    return (int)v.num_val;
}

void basic_files_close_all() {
    for (int i = 1; i <= MAX_BASIC_FILES; i++) {
        if (basic_files[i].mode != 0 && basic_files[i].fp) hal_file_close(basic_files[i].fp);
        basic_files[i].mode = 0;
        basic_files[i].fp = nullptr;
        basic_files[i].line_valid = false;
    }
}

// OPEN "ファイル名" FOR INPUT|OUTPUT|APPEND AS #n
void execute_open(const TokenList& tokens, int& pos) {
    pos++; // OPEN
    Value fname = parse_relation(tokens, pos);
    if (fname.type != Value::Type::STR) throw std::runtime_error("Type Mismatch: OPEN expects filename string");

    require_token(tokens, pos, TokenType::FOR, "Syntax Error: Expected FOR in OPEN"); pos++;
    const char* fmode;
    int mode;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::INPUT) {
        fmode = "r"; mode = 1; pos++;
    } else if (pos < tokens.size && tokens.tokens[pos].type == TokenType::IDENTIFIER &&
               strcmp(tokens.tokens[pos].text, "OUTPUT") == 0) {
        fmode = "w"; mode = 2; pos++;
    } else if (pos < tokens.size && tokens.tokens[pos].type == TokenType::IDENTIFIER &&
               strcmp(tokens.tokens[pos].text, "APPEND") == 0) {
        fmode = "a"; mode = 2; pos++;
    } else {
        throw std::runtime_error("Syntax Error: Expected INPUT, OUTPUT or APPEND");
    }
    require_token(tokens, pos, TokenType::AS, "Syntax Error: Expected AS in OPEN"); pos++;

    int n = parse_file_number(tokens, pos);
    BasicFile& f = file_slot(n);
    if (f.mode != 0) throw std::runtime_error("File already open");

    f.fp = hal_file_open(fname.str_val, fmode);
    if (!f.fp) throw std::runtime_error("File not found");
    f.mode = mode;
    f.line_valid = false;
    f.linepos = 0;
}

// CLOSE [#n [, #m ...]] — 引数なしは開いている全ファイルを閉じる
void execute_close(const TokenList& tokens, int& pos) {
    pos++; // CLOSE
    if (pos >= tokens.size || tokens.tokens[pos].type == TokenType::END_OF_FILE ||
        tokens.tokens[pos].type == TokenType::COLON || tokens.tokens[pos].type == TokenType::REM) {
        basic_files_close_all();
        return;
    }
    while (true) {
        int n = parse_file_number(tokens, pos);
        BasicFile& f = file_slot(n);
        if (f.mode != 0 && f.fp) hal_file_close(f.fp);
        f.mode = 0;
        f.fp = nullptr;
        f.line_valid = false;
        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) { pos++; continue; }
        break;
    }
}

// PRINT #n, 式 [{,|;} 式 ...] — 値を "," 区切りで 1 行に書く
void execute_print_file(const TokenList& tokens, int& pos) {
    int n = parse_file_number(tokens, pos); // pos は HASH を指して呼ばれる
    BasicFile& f = file_slot(n);
    if (f.mode == 0) throw std::runtime_error("File not open");
    if (f.mode != 2) throw std::runtime_error("Bad file mode");

    char line[256] = "";
    bool first = true;
    while (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE &&
           tokens.tokens[pos].type != TokenType::COLON &&
           tokens.tokens[pos].type != TokenType::REM) {
        if (tokens.tokens[pos].type == TokenType::COMMA ||
            tokens.tokens[pos].type == TokenType::SEMICOLON) { pos++; continue; }
        Value v = parse_relation(tokens, pos);
        if (!first) strncat(line, ",", sizeof(line) - strlen(line) - 1);
        strncat(line, v.c_str(), sizeof(line) - strlen(line) - 1);
        first = false;
    }
    if (hal_file_printf(f.fp, "%s\n", line) < 0)
        throw std::runtime_error("File Error: write failed");
}

// 1 フィールド読む（カンマまたは行末区切り）。EOF なら false
static bool file_read_field(BasicFile& f, char* out, int maxlen) {
    if (!f.line_valid) {
        if (!hal_file_gets(f.linebuf, sizeof(f.linebuf), f.fp)) return false;
        f.linepos = 0;
        f.line_valid = true;
    }
    while (f.linebuf[f.linepos] == ' ') f.linepos++;
    int o = 0;
    while (true) {
        char c = f.linebuf[f.linepos];
        if (c == '\0' || c == '\n' || c == '\r') { f.line_valid = false; break; }
        f.linepos++;
        if (c == ',') break;
        if (o < maxlen - 1) out[o++] = c;
    }
    out[o] = '\0';
    while (o > 0 && out[o - 1] == ' ') out[--o] = '\0'; // 末尾の空白を除去
    return true;
}

// INPUT #n, 変数 [, 変数 ...]
// 1 行をそのまま読む（カンマで区切らない）。
// INPUT # が途中まで読んだ行が残っていれば、その残りを返す
static bool file_read_line_raw(BasicFile& f, char* out, int maxlen) {
    if (!f.line_valid) {
        if (!hal_file_gets(f.linebuf, sizeof(f.linebuf), f.fp)) return false;
        f.linepos = 0;
        f.line_valid = true;
    }
    int o = 0;
    while (true) {
        char c = f.linebuf[f.linepos];
        if (c == '\0' || c == '\n' || c == '\r') break;
        f.linepos++;
        if (o < maxlen - 1) out[o++] = c;
    }
    out[o] = '\0';
    f.line_valid = false; // 1 行使い切った
    return true;
}

// LINE INPUT [#n,] 変数$ / LINE INPUT "プロンプト"; 変数$
//
// `INPUT #` はカンマで区切って読むため、カンマを含む文字列は復元できない。
// こちらは 1 行をそのまま 1 個の文字列として読む。
// コンソールから読む場合、`INPUT` と違ってプロンプトを指定しなければ "? " も出さない。
void execute_line_input(const TokenList& tokens, int& pos) {
    pos++; // LINE
    pos++; // INPUT

    bool from_file = false;
    int  file_no = 0;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::HASH) {
        file_no = parse_file_number(tokens, pos);
        from_file = true;
        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
    } else if (pos < tokens.size && tokens.tokens[pos].type == TokenType::STRING) {
        basic_print(tokens.tokens[pos].text);
        pos++;
        if (pos < tokens.size && (tokens.tokens[pos].type == TokenType::COMMA ||
                                  tokens.tokens[pos].type == TokenType::SEMICOLON)) pos++;
    }

    require_token(tokens, pos, TokenType::IDENTIFIER,
                  "Syntax Error: LINE INPUT expects a string variable");
    char var_name[64];
    strncpy(var_name, tokens.tokens[pos].text, sizeof(var_name) - 1);
    var_name[sizeof(var_name) - 1] = '\0';
    pos++;

    int nlen = (int)strlen(var_name);
    if (nlen == 0 || var_name[nlen - 1] != '$')
        throw std::runtime_error("Type Mismatch: LINE INPUT needs a string variable");

    int arr_idx, arr_idx2;
    parse_optional_indices(tokens, pos, arr_idx, arr_idx2);

    char buf[256] = "";
    if (from_file) {
        BasicFile& f = file_slot(file_no);
        if (f.mode == 0) throw std::runtime_error("File not open");
        if (f.mode != 1) throw std::runtime_error("Bad file mode");
        if (!file_read_line_raw(f, buf, sizeof(buf)))
            throw std::runtime_error("Input past end of file");
    } else {
        // 長押しで中断されたときは既にプログラムが止まっている（line_input.h 参照）
        if (!line_input_read_line(buf, sizeof(buf))) return;
    }

    Value val(buf);
    if (arr_idx >= 0) {
        ArrayRef* arr = get_array(var_name);
        if (!arr) throw std::runtime_error("Array not dimensioned");
        int flat_idx = flatten_array_index(arr, arr_idx, arr_idx2);
        write_heap_value(arr->start_addr + (flat_idx * 8), val);
    } else {
        set_variable(var_name, val);
    }
}

void execute_input_file(const TokenList& tokens, int& pos) {
    int n = parse_file_number(tokens, pos); // pos は HASH を指して呼ばれる
    BasicFile& f = file_slot(n);
    if (f.mode == 0) throw std::runtime_error("File not open");
    if (f.mode != 1) throw std::runtime_error("Bad file mode");
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;

    while (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE) {
        require_token(tokens, pos, TokenType::IDENTIFIER, "Syntax Error: INPUT# expects identifier");
        char var_name[64];
        strncpy(var_name, tokens.tokens[pos].text, sizeof(var_name) - 1);
        var_name[sizeof(var_name) - 1] = '\0';
        pos++;

        int arr_idx, arr_idx2;
        parse_optional_indices(tokens, pos, arr_idx, arr_idx2);

        char field[128];
        if (!file_read_field(f, field, sizeof(field)))
            throw std::runtime_error("Input past end of file");

        int nlen = strlen(var_name);
        bool is_str_var = (nlen > 0 && var_name[nlen - 1] == '$');
        bool is_int_var = (nlen > 0 && var_name[nlen - 1] == '%');
        Value val;
        if (is_str_var)      val = Value(field);
        else if (is_int_var) val = Value((int)atof(field));
        else                 val = Value((float)atof(field));

        if (arr_idx >= 0) {
            ArrayRef* arr = get_array(var_name);
            if (!arr) throw std::runtime_error("Array not dimensioned");
            int flat_idx = flatten_array_index(arr, arr_idx, arr_idx2);
            write_heap_value(arr->start_addr + (flat_idx * 8), val);
        } else {
            set_variable(var_name, val);
        }

        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
        else break;
    }
}

// EOF(n): これ以上読むものが無ければ 1。次の行を先読みしてバッファに保持する
int basic_file_eof(int fileno) {
    BasicFile& f = file_slot(fileno);
    if (f.mode == 0) throw std::runtime_error("File not open");
    if (f.mode != 1) return 1; // 書き込み用は常に終端扱い

    if (f.line_valid) {
        // 現在行に空白以外の未読があれば「まだある」
        int p = f.linepos;
        while (f.linebuf[p] == ' ') p++;
        if (f.linebuf[p] != '\0' && f.linebuf[p] != '\n' && f.linebuf[p] != '\r') return 0;
        f.line_valid = false; // 空白だけなら行を使い切ったとみなす
    }
    if (!hal_file_gets(f.linebuf, sizeof(f.linebuf), f.fp)) return 1;
    f.linepos = 0;
    f.line_valid = true;
    return 0;
}

void execute_save(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::STRING, "Syntax Error: SAVE expects filename string");
    const char* filename = tokens.tokens[pos].text;
    pos++;
    
    void* fp = hal_file_open(filename, "w");
    if (!fp) throw std::runtime_error("File Error: Cannot open file for writing");
    
    uint16_t ptr = MEMORY_TEXT_BASE;
    while (true) {
        if (logical_memory[ptr+2] == 0 && logical_memory[ptr+3] == 0 && ptr != MEMORY_TEXT_BASE) break;

        uint16_t line_num = prog_line_no(ptr);
        hal_file_printf(fp, "%d", line_num);

        TokenList t = get_detokenized_line(ptr); // from program_manager.cpp
        for (int i=0; i<t.size; i++) {
            if (t.tokens[i].type == TokenType::END_OF_FILE) break;
            // 書き戻しは LIST と同じ関数に任せる。ここで独自に組み立てていた
            // ころは REM の分岐が抜けていて、保存するとコメントが消えていた。
            //
            // 区切りの空白はトークンの「前」に置く。後ろに置くと行末にも空白が
            // 残り、それが REM の本文に取り込まれて、保存するたびに 1 つずつ
            // 伸びていく
            char rendered[MAX_TOKEN_LEN + 8];
            token_to_source(rendered, sizeof(rendered), t.tokens[i]);
            hal_file_printf(fp, " %s", rendered);
        }
        hal_file_printf(fp, "\n");
        
        uint16_t next_ptr = prog_next_ptr(ptr);
        if (next_ptr == 0) break;
        ptr = next_ptr;
    }
    hal_file_close(fp);
    basic_print("Saved\n");
}

void execute_load(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::STRING, "Syntax Error: LOAD expects filename string");
    const char* filename = tokens.tokens[pos].text;
    pos++;
    
    void* fp = hal_file_open(filename, "r");
    if (!fp) throw std::runtime_error("File Error: Cannot open file for reading");
    
    clear_program();
    char line_buf[256];
    while (hal_file_gets(line_buf, sizeof(line_buf), fp)) {
        // 行末の改行を落としてから字句解析する。REM は行末までを本文として
        // 取り込むので、残っていると改行そのものがコメントに入り、保存と
        // 読み込みを繰り返すたびに伸びていく。PC で編集した CRLF のファイルを
        // 読んだときの \r も同じ理由でここで落とす
        int len = (int)strlen(line_buf);
        while (len > 0 && (line_buf[len-1] == '\n' || line_buf[len-1] == '\r'))
            line_buf[--len] = '\0';

        TokenList t = lex(line_buf);
        if (t.size > 0 && t.tokens[0].type == TokenType::NUMBER) {
            int line_num = atoi(t.tokens[0].text);
            TokenList remainder;
            int j = 0;
            for (int i = 1; i < t.size; i++) {
                if (t.tokens[i].type == TokenType::END_OF_FILE) break;
                remainder.tokens[j++] = t.tokens[i];
            }
            remainder.size = j;
            store_line(line_num, remainder);
        }
    }
    hal_file_close(fp);
    basic_print("Loaded\n");
}

void execute_kill(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::STRING, "Syntax Error: KILL expects filename string");
    const char* filename = tokens.tokens[pos].text;
    pos++;
    
    if (hal_file_remove(filename) != 0) {
        throw std::runtime_error("File Error: Cannot delete file");
    }
    basic_print("Deleted\n");
}

void execute_name(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::STRING, "Syntax Error: NAME expects old filename string");
    char oldname[128];
    strncpy(oldname, tokens.tokens[pos].text, sizeof(oldname)-1);
    pos++;
    
    require_token(tokens, pos, TokenType::AS, "Syntax Error: Expected AS in NAME command");
    pos++;
    
    require_token(tokens, pos, TokenType::STRING, "Syntax Error: NAME expects new filename string");
    const char* newname = tokens.tokens[pos].text;
    pos++;
    
    if (hal_file_rename(oldname, newname) != 0) {
        throw std::runtime_error("File Error: Cannot rename file");
    }
    basic_print("Renamed\n");
}

void execute_files(const TokenList&, int& pos) {
    pos++; 
    
    void* dir = hal_dir_open(".");
    if (dir == NULL) {
        basic_print("Error: Cannot open directory\n");
        return;
    }
    
    // 桁幅 16 × 2 列 = 32 桁。LCD は 40 桁なので途中で折り返さない
    const int COL_WIDTH = 16;
    const int COLS_PER_ROW = 2;

    const char* d_name;
    int count = 0;      // 見つかったファイル数
    int col = 0;        // 現在の行に出した個数
    char buf[128];

    while ((d_name = hal_dir_read(dir)) != NULL) {
        count++;

        if (strlen(d_name) >= (size_t)COL_WIDTH) {
            // 桁に収まらない長い名前は独立した行に出す。
            // 詰めて出すと隣のファイル名と繋がって読めなくなるため
            if (col != 0) {
                basic_print("\n");
                col = 0;
            }
            snprintf(buf, sizeof(buf), "%s\n", d_name);
            basic_print(buf);
            continue;
        }

        snprintf(buf, sizeof(buf), "%-*s", COL_WIDTH, d_name);
        basic_print(buf);
        if (++col >= COLS_PER_ROW) {
            basic_print("\n");
            col = 0;
        }
    }
    if (col != 0) basic_print("\n");

    hal_dir_close(dir);
    snprintf(buf, sizeof(buf), "%d File(s) found\n", count);
    basic_print(buf);
}

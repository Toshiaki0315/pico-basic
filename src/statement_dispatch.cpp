#include "parser_internal.h"
#include "hal_display.h"
#include <stdexcept>
#include <cstring>
#include <cstdio>

// トークンから実行関数への振り分け。
//
// 実装はすべて statement_*.cpp 側にあり、宣言は parser_internal.h にある。
// 新しい命令を足すときに触るのはここと lexer.cpp のキーワード表だけで済むよう、
// 振り分けだけを 1 ファイルに切り出してある。

void execute_not_implemented(const TokenList& tokens, int& pos) {
    char buf[128];
    snprintf(buf, sizeof(buf), "Notice: Command '%s' is registered but not yet implemented.\n", tokens.tokens[pos].text);
    basic_print(buf);
    pos = tokens.size;
}

// 互換のための空実行。命令と（あれば）引数を次の `:` まで読み飛ばし、何もしない。
// INIT / NEWON は元の X1 Hu-BASIC ではメモリ領域予約の命令だが、この実装には
// 対応する概念がないため受理して無視する（古いプログラムがエラーで止まらないように）。
void execute_noop_statement(const TokenList& tokens, int& pos) {
    pos++; // 命令トークン
    while (pos < tokens.size && tokens.tokens[pos].type != TokenType::COLON &&
           tokens.tokens[pos].type != TokenType::END_OF_FILE) {
        pos++;
    }
}

void execute_statement(const TokenList& tokens, int& pos) {
    if (pos >= tokens.size || tokens.tokens[pos].type == TokenType::END_OF_FILE) return;

    switch (tokens.tokens[pos].type) {
        case TokenType::PRINT:   execute_print(tokens, pos); break;
        case TokenType::DATA:    pos = tokens.size; break; 
        case TokenType::RESTORE: pos++; data_ptr = 0; break;
        case TokenType::READ:    execute_read(tokens, pos); break;
        case TokenType::GOTO:    execute_goto(tokens, pos); break;
        case TokenType::GOSUB:   execute_gosub(tokens, pos); break;
        case TokenType::RETURN:  execute_return(tokens, pos); break;
        case TokenType::IF:      execute_if(tokens, pos); break;
        case TokenType::FILES:   execute_files(tokens, pos); break;
        case TokenType::KILL:    execute_kill(tokens, pos); break;
        case TokenType::NAME:    execute_name(tokens, pos); break;
        case TokenType::ON:      execute_on(tokens, pos); break;
        case TokenType::FOR:     execute_for(tokens, pos); break;
        case TokenType::NEXT:    execute_next(tokens, pos); break;
        case TokenType::DIM:     execute_dim(tokens, pos); break;
        case TokenType::INPUT:   execute_input(tokens, pos); break;
        case TokenType::END:     execute_end(tokens, pos); break;
        case TokenType::STOP:    execute_stop(tokens, pos); break;
        case TokenType::LET:     execute_assignment(tokens, pos, true); break;
        case TokenType::IDENTIFIER:
            if (strcmp(tokens.tokens[pos].text, "MID$") == 0) execute_mid_statement(tokens, pos);
            // 引数なしの BATTERY は状態表示の文。`BATTERY(n)` は関数なので式側に任せる
            else if (strcmp(tokens.tokens[pos].text, "BATTERY") == 0 &&
                     (pos + 1 >= tokens.size || tokens.tokens[pos + 1].type != TokenType::LPAREN))
                execute_battery_status(tokens, pos);
            // IMU も同様に、引数なしなら状態表示の文
            else if (strcmp(tokens.tokens[pos].text, "IMU") == 0 &&
                     (pos + 1 >= tokens.size || tokens.tokens[pos + 1].type != TokenType::LPAREN))
                execute_imu_status(tokens, pos);
            else if (strcmp(tokens.tokens[pos].text, "RTC") == 0 &&
                     (pos + 1 >= tokens.size || tokens.tokens[pos + 1].type != TokenType::LPAREN))
                execute_rtc_status(tokens, pos);
            // TIME$ / DATE$ への代入は時計合わせ（読み出しは式側で処理する）
            else if ((strcmp(tokens.tokens[pos].text, "TIME$") == 0 ||
                      strcmp(tokens.tokens[pos].text, "DATE$") == 0) &&
                     pos + 1 < tokens.size && tokens.tokens[pos + 1].type == TokenType::ASSIGN)
                execute_rtc_set(tokens, pos);
            else execute_assignment(tokens, pos, false);
            break;
        
        case TokenType::COLOR:   execute_color(tokens, pos); break;
        case TokenType::PSET:    execute_pset(tokens, pos); break;
        case TokenType::LINE:
            // `LINE INPUT` は 1 行読み込み、それ以外は図形の LINE
            if (pos + 1 < tokens.size && tokens.tokens[pos + 1].type == TokenType::INPUT)
                execute_line_input(tokens, pos);
            else
                execute_line(tokens, pos);
            break;
        case TokenType::CIRCLE:  execute_circle(tokens, pos); break;
        case TokenType::CLS:     pos++; hal_display_cls(); break;
        case TokenType::LOCATE:  execute_locate(tokens, pos); break;
        case TokenType::WAIT:    execute_wait(tokens, pos); break;
        case TokenType::CLEAR:   pos++; execute_clear(); break;
        
        case TokenType::GPIO:    execute_gpio(tokens, pos); break;
        case TokenType::BRIGHTNESS: execute_brightness(tokens, pos); break;
        case TokenType::PAINT:   execute_paint(tokens, pos); break;
        case TokenType::GET_AT:  execute_get_at(tokens, pos); break;
        case TokenType::PUT_AT:  execute_put_at(tokens, pos); break;
        
        case TokenType::SAVE:    execute_save(tokens, pos); break;
        case TokenType::OPEN:    execute_open(tokens, pos); break;
        case TokenType::CLOSE:   execute_close(tokens, pos); break;
        case TokenType::DELETE_CMD: execute_delete(tokens, pos); break;
        case TokenType::TRON:    pos++; trace_enabled = true; break;
        case TokenType::RANDOMIZE: execute_randomize(tokens, pos); break;
        case TokenType::SYNC:    execute_sync(tokens, pos); break;
        case TokenType::POWEROFF: execute_poweroff(tokens, pos); break;
        case TokenType::TROFF:   pos++; trace_enabled = false; break;
        case TokenType::LOAD:    execute_load(tokens, pos); break;

        case TokenType::INIT: case TokenType::NEWON:
                                 execute_noop_statement(tokens, pos); break;
        case TokenType::LABEL:   pos++; break; // 行頭のラベル定義は実行時は素通り
        case TokenType::REM:     pos = tokens.size; break; // コメント。行末まで読み飛ばす
        case TokenType::DEF:     execute_def(tokens, pos); break;
        case TokenType::POKE:    execute_poke(tokens, pos); break;
        case TokenType::AUTO:    execute_noop_statement(tokens, pos); break; // AUTO は repl が処理。プログラム内では無視
        case TokenType::WIDTH:   execute_width(tokens, pos); break;
        case TokenType::CONSOLE: execute_console(tokens, pos); break;
        case TokenType::REPEAT:  execute_repeat(tokens, pos); break;
        case TokenType::UNTIL:   execute_until(tokens, pos); break;
        case TokenType::WHILE:   execute_while(tokens, pos); break;
        case TokenType::WEND:    execute_wend(tokens, pos); break;
        case TokenType::RESUME:  execute_resume(tokens, pos); break;
        case TokenType::GET:     execute_get(tokens, pos); break;
        case TokenType::WINDOW:  execute_window(tokens, pos); break;
        case TokenType::POLY:    execute_poly(tokens, pos); break;
        case TokenType::BEEP:    execute_beep(tokens, pos); break;
        case TokenType::PLAY:    execute_music(tokens, pos); break; 
        case TokenType::MUSIC:   execute_music(tokens, pos); break;
        case TokenType::SOUND:   execute_sound(tokens, pos); break;

        default: throw std::runtime_error("Syntax Error: Unrecognized statement");
    }
}

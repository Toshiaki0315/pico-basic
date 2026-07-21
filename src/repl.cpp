#include "repl.h"
#include "hal_display.h"
#include "hal_sound.h"
#include "lexer.h"
#include "parser.h"
#include <stdio.h>
#include <cstring>
#include <stdexcept>

#define MAX_LINE_LEN 256

void repl_start() {
    static char input_buffer[MAX_LINE_LEN];
    int input_ptr = 0;
    
    // Output initial startup banner.
    // ビルド日時を出すのは、実機に焼かれているファームが
    // どのビルドなのかを不具合調査時に判別できるようにするため
    static char banner[64];
    snprintf(banner, sizeof(banner), "pico-basic v2.0 (%s %s)\n", __DATE__, __TIME__);
    printf("%s", banner);
    hal_display_print(banner);

    bool show_ready = true;
    // 直前に行を確定させた改行コード。CRLF / LFCR を送る端末で、
    // 相方の改行を「空行の入力」として扱わないために覚えておく
    int last_terminator = 0;

    while (true) {
        if (show_ready) {
            const char* ready = "Ready\n";
            printf("%s", ready);
            hal_display_print(ready);
        }
        
        // Wait for a full line of input
        input_ptr = 0;
        memset(input_buffer, 0, MAX_LINE_LEN);
        
        while (true) {
            int c = getchar(); // USB CDC Blocking Input
            
            if (c == EOF) {
                continue; // Prevent infinite loop on EOF
            }
            
            // Simple Line Editor implementation
            if (c == 0x03) { // Ctrl-C
                // 非同期で鳴っている演奏を止める。
                // RUN 中の中断は run_program() 側で処理しているが、
                // ダイレクトモードで PLAY した音はここでしか止められない
                hal_sound_stop();

                // 入力途中の行は破棄して Ready に戻る
                input_ptr = 0;
                input_buffer[0] = '\0';
                last_terminator = 0;
                printf("\n");
                hal_display_print("\n");
                break;
            } else if (c == '\r' || c == '\n') {
                // CRLF / LFCR の 2 文字目は、直前の確定と対になる改行なので読み飛ばす。
                // これをしないと空行を入力したものとして扱われ、Ready が余分に出る
                if (input_ptr == 0 && last_terminator != 0 && c != last_terminator) {
                    last_terminator = 0;
                    continue;
                }
                last_terminator = c;
                printf("\n");
                hal_display_print("\n");
                break;
            } else if (c == '\b' || c == 127) { // Backspace
                if (input_ptr > 0) {
                    input_ptr--;
                    input_buffer[input_ptr] = '\0';
                    printf("\b \b");
                    hal_display_print("\b \b");
                }
            } else if (c >= 32 && c <= 126) {
                last_terminator = 0; // 改行の対を待つ状態を解除する
                if (input_ptr < MAX_LINE_LEN - 1) {
                    input_buffer[input_ptr++] = static_cast<char>(c);
                    input_buffer[input_ptr] = '\0';
                    putchar(c); // Echo back
                    
                    // Echo to LCD
                    char s[2] = {static_cast<char>(c), '\0'};
                    hal_display_print(s);
                }
            }
        }

        if (input_ptr > 0) {
            // lex() は不正な変数名などで例外を投げる。ここで捕まえないと
            // std::terminate でインタプリタごと落ち、電源を切るしかなくなる
            try {
                TokenList tokens = lex(input_buffer);

                // Parse & Execute.
                // Returns true if a line was stored (line-number mode) -> suppress Ready
                bool line_stored = parse_and_execute(tokens);
                show_ready = !line_stored;
            } catch (const std::exception& e) {
                char buf[160];
                snprintf(buf, sizeof(buf), "%s\n", e.what());
                basic_print(buf);
                show_ready = true;
            }
        } else {
            show_ready = true;
        }
    }
}

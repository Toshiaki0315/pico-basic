#include "repl.h"
#include "kana_utf8.h"
#include "hal_battery.h"
#include "hal_display.h"
#include "hal_sound.h"
#include "lexer.h"
#include "parser.h"
#include "screenshot.h"
#include <stdio.h>
#include <cstring>
#include <cctype>
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

    // AUTO（行番号自動生成）モードの状態。auto_active の間は各行入力の前に
    // 「番号 」を自動で流し込み、確定するたびに auto_num を auto_step だけ進める。
    bool auto_active = false;
    int  auto_num  = 10;
    int  auto_step = 10;

    while (true) {
        if (show_ready && !auto_active) {
            const char* ready = "Ready\n";
            printf("%s", ready);
            hal_display_print(ready);
        }

        // Wait for a full line of input
        input_ptr = 0;
        memset(input_buffer, 0, MAX_LINE_LEN);

        // AUTO モードなら行番号をプロンプトとして先頭に流し込む
        if (auto_active) {
            if (auto_num > 65535) { // 行番号の上限を超えたら AUTO を終える
                auto_active = false;
                show_ready = true;
                continue;
            }
            input_ptr = snprintf(input_buffer, MAX_LINE_LEN, "%d ", auto_num);
            printf("%s", input_buffer);
            hal_display_print(input_buffer);
        }
        // 行頭の位置（AUTO のときは番号ぶんだけ後ろ）。改行対処と BS の下限に使う
        int base = input_ptr;
        bool aborted = false;

        while (true) {
            // 文字が来るまで待つ間、電源ボタンの長押しを監視する。
            // ここを素の getchar() にすると Ready の待ち受け中に反応しなくなる
            int c;
            while ((c = hal_system_getchar_timeout(50000)) < 0) {
                if (hal_battery_power_key_held()) basic_power_off();
            }

            if (c == EOF) {
                continue; // Prevent infinite loop on EOF
            }

            // 端末は UTF-8 で送ってくる。半角カタカナ（3 バイト）は JIS の 1 バイトに
            // 畳んでから通常の経路へ流す。漢字・ひらがなは表示できないので 3 バイトごと捨てる。
            // 2 バイトしか読み捨てないと 1 バイトずれて別の字に化ける（`ﾀﾁﾂ` → `ｾ`）
            if (c >= 0xE0 && c <= 0xEF) {
                int b2 = getchar();
                int b3 = getchar();
                unsigned char kana = utf8_to_jis_kana((unsigned char)c, (unsigned char)b2, (unsigned char)b3);
                if (kana == 0) continue;
                c = kana;
            }

            // Simple Line Editor implementation
            if (c == 0x03) { // Ctrl-C
                // 非同期で鳴っている演奏を止める。
                // RUN 中の中断は run_program() 側で処理しているが、
                // ダイレクトモードで PLAY した音はここでしか止められない
                hal_sound_stop();

                // AUTO 中の Ctrl-C は自動行番号モードを終える（BREAK 相当）
                auto_active = false;

                // 入力途中の行は破棄して Ready に戻る
                input_ptr = 0;
                input_buffer[0] = '\0';
                last_terminator = 0;
                printf("\n");
                hal_display_print("\n");
                aborted = true;
                break;
            } else if (c == '\r' || c == '\n') {
                // CRLF / LFCR の 2 文字目は、直前の確定と対になる改行なので読み飛ばす。
                // これをしないと空行を入力したものとして扱われ、Ready が余分に出る
                if (input_ptr == base && last_terminator != 0 && c != last_terminator) {
                    last_terminator = 0;
                    continue;
                }
                last_terminator = c;
                printf("\n");
                hal_display_print("\n");
                break;
            } else if (c == '\b' || c == 127) { // Backspace
                // AUTO の行番号プロンプト（base より前）は消させない
                if (input_ptr > base) {
                    input_ptr--;
                    input_buffer[input_ptr] = '\0';
                    printf("\b \b");
                    hal_display_print("\b \b");
                }
            } else if (c == 0x10) { // Ctrl-P: 画面を BMP で SD に保存
                // 先に撮ってから知らせる（メッセージが写り込まないように）。
                // 通知はシリアルだけに出し、LCD の内容は一切触らない
                char shot[16];
                if (screenshot_save_next(shot, sizeof(shot)))
                    printf("\n[screen saved: %s]\n", shot);
                else
                    printf("\n[screen save failed]\n");
                // 入力途中の行を打ち直さずに済むよう、そのまま再表示する
                if (input_ptr > 0) serial_print_kana(input_buffer);
            } else if (c >= 0x81 && c <= 0x9F) {
                // Shift-JIS を送ってくる端末向けの保険。2 バイト文字の 1 バイト目なので
                // 対のバイトも読み捨てる（放置すると 2 バイト目が単独の字として紛れ込む）
                getchar();
            } else if ((c >= 32 && c <= 126) || (c >= 0xA1 && c <= 0xDF)) {
                // 印字可能 ASCII と JIS X 0201 半角カタカナ（0xA1-0xDF）を受け付ける
                last_terminator = 0; // 改行の対を待つ状態を解除する
                if (input_ptr < MAX_LINE_LEN - 1) {
                    input_buffer[input_ptr++] = static_cast<char>(c);
                    input_buffer[input_ptr] = '\0';
                    // エコーもシリアルへ出るので、半角カタカナは UTF-8 に直して送る。
                    // 生の JIS バイトのままだと端末には不正な UTF-8 として届く
                    serial_putc_kana(static_cast<unsigned char>(c));

                    // Echo to LCD
                    char s[2] = {static_cast<char>(c), '\0'};
                    hal_display_print(s);
                }
            }
        }

        if (aborted) {
            show_ready = true;
            continue;
        }

        // AUTO モード中の行確定
        if (auto_active) {
            // 番号のあとに中身が無ければ（空行）AUTO を終える
            bool empty = true;
            for (int i = base; i < input_ptr; i++) {
                if (!isspace(static_cast<unsigned char>(input_buffer[i]))) { empty = false; break; }
            }
            if (empty) {
                auto_active = false;
                show_ready = true;
                continue;
            }
            try {
                parse_and_execute(lex(input_buffer)); // 行を格納する
            } catch (const std::exception& e) {
                // 例外で抜けても画面が固まらないよう、転送の遅延は必ず解除する
                hal_display_set_deferred(false);
                char buf[160];
                snprintf(buf, sizeof(buf), "%s\n", e.what());
                basic_print(buf);
            }
            auto_num += auto_step;
            show_ready = false;
            continue;
        }

        if (input_ptr > 0) {
            // lex() は不正な変数名などで例外を投げる。ここで捕まえないと
            // std::terminate でインタプリタごと落ち、電源を切るしかなくなる
            try {
                TokenList tokens = lex(input_buffer);

                // Parse & Execute.
                // Returns true if a line was stored (line-number mode) -> suppress Ready
                bool line_stored = parse_and_execute(tokens);
                // ダイレクトモードで SYNC OFF のままにすると、以降のキー入力の
                // エコーまで画面に出なくなる。Ready に戻る前に必ず解除する
                hal_display_set_deferred(false);
                show_ready = !line_stored;

                // AUTO コマンドが実行されたら行番号自動生成モードに入る
                int start, step;
                if (auto_mode_requested(&start, &step)) {
                    auto_active = true;
                    auto_num = start;
                    auto_step = step;
                    show_ready = false;
                }
            } catch (const std::exception& e) {
                // 例外で抜けても画面が固まらないよう、転送の遅延は必ず解除する
                hal_display_set_deferred(false);
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

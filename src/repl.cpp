#include "repl.h"
#include "kana_utf8.h"
#include "line_input.h"
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

// 端末と LCD の両方へ出す。REPL の表示はどれも両方に出る
static void echo_both(const char* text) {
    printf("%s", text);
    hal_display_print(text);
}

// 起動バナー。ビルド日時を出すのは、実機に焼かれているファームがどのビルドなのか
// 不具合調査時に判別できるようにするため
static void print_banner() {
    char banner[64];
    snprintf(banner, sizeof(banner), "pico-basic v2.0 (%s %s)\n", __DATE__, __TIME__);
    echo_both(banner);
}

// AUTO モードで行番号をプロンプトとして流し込む。戻り値は流し込んだ長さ
static int write_auto_prompt(char* buffer, int size, int line_no) {
    int n = snprintf(buffer, size, "%d ", line_no);
    echo_both(buffer);
    return n;
}

// エラー表示。例外で抜けても画面が固まらないよう、転送の遅延は必ず解除する
static void report_repl_error(const std::exception& e) {
    hal_display_set_deferred(false);
    char buf[160];
    snprintf(buf, sizeof(buf), "%s\n", e.what());
    basic_print(buf);
}

// 1 行入力できるまで編集を続ける。改行で確定したら true。
// Ctrl-C か電源ボタンで捨てられたら false（呼び出し側は Ready へ戻る）。
//
// base は行頭の位置。AUTO のときは自動で流し込んだ行番号のぶんだけ後ろにあり、
// バックスペースでそこより前を消させないための下限になる。
static bool repl_edit_line(char* buffer, int& length, int base,
                           int& last_terminator, bool& auto_active) {
    while (true) {
        // 文字が来るまで待つ。待っている間の電源ボタンの監視も、端末が
        // 送ってくる多バイト文字の畳み込みも line_input.cpp が面倒を見る
        int c = line_input_getchar();
        if (c == LINE_INPUT_SKIP) continue;
        // 電源を落としに行ったが切れなかった。入力途中の行は捨てて Ready へ
        if (c == LINE_INPUT_POWER_OFF) return false;

        if (c == 0x03) { // Ctrl-C
            // 非同期で鳴っている演奏を止める。
            // RUN 中の中断は run_program() 側で処理しているが、
            // ダイレクトモードで PLAY した音はここでしか止められない
            hal_sound_stop();
            auto_active = false; // AUTO 中の Ctrl-C は自動行番号モードを終える
            length = 0;
            buffer[0] = '\0';
            last_terminator = 0;
            echo_both("\n");
            return false;
        }

        if (c == '\r' || c == '\n') {
            // CRLF / LFCR の 2 文字目は、直前の確定と対になる改行なので読み飛ばす。
            // これをしないと空行を入力したものとして扱われ、Ready が余分に出る
            if (length == base && last_terminator != 0 && c != last_terminator) {
                last_terminator = 0;
                continue;
            }
            last_terminator = c;
            echo_both("\n");
            return true;
        }

        if (c == '\b' || c == 127) { // Backspace
            // AUTO の行番号プロンプト（base より前）は消させない
            if (length > base) {
                length--;
                buffer[length] = '\0';
                echo_both("\b \b");
            }
            continue;
        }

        if (c == 0x10) { // Ctrl-P: 画面を BMP で SD に保存
            // 先に撮ってから知らせる（メッセージが写り込まないように）。
            // 通知はシリアルだけに出し、LCD の内容は一切触らない
            char shot[16];
            if (screenshot_save_next(shot, sizeof(shot)))
                printf("\n[screen saved: %s]\n", shot);
            else
                printf("\n[screen save failed]\n");
            // 入力途中の行を打ち直さずに済むよう、そのまま再表示する
            if (length > 0) serial_print_kana(buffer);
            continue;
        }

        // 印字可能 ASCII と JIS X 0201 半角カタカナ（0xA1-0xDF）を受け付ける
        bool printable = (c >= 32 && c <= 126) || (c >= 0xA1 && c <= 0xDF);
        if (printable && length < MAX_LINE_LEN - 1) {
            last_terminator = 0; // 改行の対を待つ状態を解除する
            buffer[length++] = static_cast<char>(c);
            buffer[length] = '\0';
            // エコーもシリアルへ出るので、半角カタカナは UTF-8 に直して送る。
            // 生の JIS バイトのままだと端末には不正な UTF-8 として届く
            serial_putc_kana(static_cast<unsigned char>(c));
            char s[2] = {static_cast<char>(c), '\0'};
            hal_display_print(s);
        }
    }
}

// AUTO モードで確定した 1 行を格納する。空行なら AUTO を終える（false を返す）
static bool repl_store_auto_line(const char* buffer, int base, int length) {
    bool empty = true;
    for (int i = base; i < length; i++) {
        if (!isspace(static_cast<unsigned char>(buffer[i]))) { empty = false; break; }
    }
    if (empty) return false;

    try {
        parse_and_execute(lex(buffer)); // 行を格納する
    } catch (const std::exception& e) {
        report_repl_error(e);
    }
    return true;
}

void repl_start() {
    static char input_buffer[MAX_LINE_LEN];
    int input_ptr = 0;

    print_banner();

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
        if (show_ready && !auto_active) echo_both("Ready\n");

        input_ptr = 0;
        memset(input_buffer, 0, MAX_LINE_LEN);

        if (auto_active) {
            if (auto_num > 65535) { // 行番号の上限を超えたら AUTO を終える
                auto_active = false;
                show_ready = true;
                continue;
            }
            input_ptr = write_auto_prompt(input_buffer, MAX_LINE_LEN, auto_num);
        }
        // 行頭の位置（AUTO のときは番号ぶんだけ後ろ）。改行対処と BS の下限に使う
        int base = input_ptr;

        bool aborted = !repl_edit_line(input_buffer, input_ptr, base,
                                       last_terminator, auto_active);

        if (aborted) {
            show_ready = true;
            continue;
        }

        // AUTO モード中の行確定
        if (auto_active) {
            if (!repl_store_auto_line(input_buffer, base, input_ptr)) {
                auto_active = false;
                show_ready = true;
                continue;
            }
            auto_num += auto_step;
            show_ready = false;
            continue;
        }

        if (input_ptr == 0) {
            show_ready = true;
            continue;
        }

        // lex() は不正な変数名などで例外を投げる。ここで捕まえないと
        // std::terminate でインタプリタごと落ち、電源を切るしかなくなる
        try {
            // 行番号つきなら格納されるだけ。そのときは Ready を出さない
            bool line_stored = parse_and_execute(lex(input_buffer));
            // ダイレクトモードで SYNC OFF のままにすると、以降のキー入力の
            // エコーまで画面に出なくなる。Ready に戻る前に必ず解除する
            hal_display_set_deferred(false);
            show_ready = !line_stored;

            // AUTO コマンドが実行されたら行番号自動生成モードに入る
            int start, step;
            if (auto_mode_requested(&start, &step)) {
                auto_active = true;
                auto_num  = start;
                auto_step = step;
                show_ready = false;
            }
        } catch (const std::exception& e) {
            report_repl_error(e);
            show_ready = true;
        }
    }
}

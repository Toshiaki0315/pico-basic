#include "line_input.h"
#include "hal_battery.h"
#include "hal_display.h"
#include "kana_utf8.h"
#include "parser.h"
#include <stdio.h>
#include <string.h>

// 電源ボタンを見に行く間隔。人が押している 2 秒に対して十分細かく、
// かつ 1 文字ごとにタイマを回しすぎない程度の値
#define POLL_INTERVAL_US 50000

// 1 バイト届くまで待つ。待っている間だけ電源ボタンを見られる
static int wait_byte() {
    for (;;) {
        int c = hal_system_getchar_timeout(POLL_INTERVAL_US);
        if (c >= 0) return c;
        if (hal_battery_power_key_held()) return LINE_INPUT_POWER_OFF;
    }
}

// 多バイト文字の 2 バイト目以降を読む。長押しで中断されたら false
static bool wait_trailing_bytes(int* out, int count) {
    for (int i = 0; i < count; i++) {
        int b = wait_byte();
        if (b == LINE_INPUT_POWER_OFF) return false;
        if (out) out[i] = b;
    }
    return true;
}

int line_input_getchar() {
    int c = wait_byte();
    if (c == LINE_INPUT_POWER_OFF) {
        basic_power_off(); // 電源が落ちればここから戻らない
        return LINE_INPUT_POWER_OFF;
    }

    // 端末は UTF-8 で送ってくる。半角カタカナ（3 バイト）は JIS の 1 バイトに
    // 畳んでから通常の経路へ流す。漢字・ひらがなは表示できないので 3 バイトごと捨てる。
    // 2 バイトしか読み捨てないと 1 バイトずれて別の字に化ける（`ﾀﾁﾂ` → `ｾ`）
    if (c >= 0xE0 && c <= 0xEF) {
        int rest[2];
        if (!wait_trailing_bytes(rest, 2)) {
            basic_power_off();
            return LINE_INPUT_POWER_OFF;
        }
        unsigned char kana = utf8_to_jis_kana((unsigned char)c,
                                              (unsigned char)rest[0],
                                              (unsigned char)rest[1]);
        return (kana == 0) ? LINE_INPUT_SKIP : (int)kana;
    }

    // Shift-JIS を送ってくる端末向けの保険。2 バイト文字の 1 バイト目なので
    // 対のバイトも読み捨てる（放置すると 2 バイト目が単独の字として紛れ込む）
    if (c >= 0x81 && c <= 0x9F) {
        if (!wait_trailing_bytes(nullptr, 1)) {
            basic_power_off();
            return LINE_INPUT_POWER_OFF;
        }
        return LINE_INPUT_SKIP;
    }

    return c;
}

// 入力した 1 文字をシリアルと LCD の両方へ返す。
// エコーもシリアルへ出るので、半角カタカナは UTF-8 に直して送る
// （生の JIS バイトのままだと端末には不正な UTF-8 として届く）
static void echo_char(int c) {
    serial_putc_kana((unsigned char)c);
    char s[2] = { (char)c, '\0' };
    hal_display_print(s);
}

bool line_input_read_line(char* buffer, int max_len) {
    int input_ptr = 0;
    memset(buffer, 0, max_len);

    for (;;) {
        int c = line_input_getchar();
        if (c == LINE_INPUT_POWER_OFF) return false;
        if (c == LINE_INPUT_SKIP) continue;

        if (c == '\r' || c == '\n') {
            printf("\n");
            hal_display_print("\n");
            return true;
        }

        if (c == '\b' || c == 127) {
            if (input_ptr > 0) {
                input_ptr--;
                buffer[input_ptr] = '\0';
                printf("\b \b");
                hal_display_print("\b \b");
            }
            continue;
        }

        // 印字可能 ASCII と JIS X 0201 半角カタカナ（0xA1-0xDF）だけ受け付ける
        bool printable = (c >= 32 && c <= 126) || (c >= 0xA1 && c <= 0xDF);
        if (printable && input_ptr < max_len - 1) {
            buffer[input_ptr++] = (char)c;
            buffer[input_ptr] = '\0';
            echo_char(c);
        }
    }
}

#include "parser_internal.h"
#include "parser.h"
#include "hal_display.h"
#include "hal_battery.h"
#include "hal_imu.h"
#include "hal_rtc.h"
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// 本体まわりの状態を見る・変える文。
// BATTERY / IMU / RTC の表示、RTC の設定、SYNC（画面転送の制御）、
// RANDOMIZE（乱数の種）、POWEROFF（電源を切る）。
//
// 言語としての意味はほとんど無く、HAL を BASIC から触るための入り口が集まる。

// 引数なしの `BATTERY` — 電池の状態をまとめて表示する。
// 40 桁に収めるため 2 行に分ける。
void execute_battery_status(const TokenList&, int& pos) {
    pos++; // BATTERY
    int mv  = hal_battery_millivolts();
    int usb = hal_battery_usb_connected();

    // 「無し」は返らない（電池が無いことは証明できない。hal_battery.h 参照）
    const char* cell = (battery_presence(mv, usb) == 1) ? "OK" : "UNKNOWN";

    // 10mV 単位に四捨五入してから小数 2 桁にする（切り捨てると 4196 が 4.19 になる）
    int cv = (mv + 5) / 10;

    char buf[96];
    snprintf(buf, sizeof(buf), "BATTERY %d.%02dV (%d%%)\n",
             cv / 100, cv % 100, battery_percent_from_mv(mv));
    basic_print(buf);
    snprintf(buf, sizeof(buf), "SOURCE  %-4s  CELL %s\n",
             usb ? "USB" : "BATT", cell);
    basic_print(buf);
}

// 引数なしの `IMU` — 6 軸の値をまとめて表示する（BATTERY と同じ形）。
// 傾け方と数値の対応を実機で確かめるときの入口。
void execute_imu_status(const TokenList&, int& pos) {
    pos++; // IMU
    char buf[96];
    if (!hal_imu_present()) {
        // 初期化をやり直したうえで、バスの状態を出して切り分けられるようにする
        unsigned char who = 0;
        unsigned char found[16];
        int n = hal_imu_diagnose(found, 16, &who);
        if (!hal_imu_present()) {
            basic_print("IMU NOT FOUND\n");
            snprintf(buf, sizeof(buf), "WHO_AM_I %02X (EXPECT 05) AT 6A/6B\n", who);
            basic_print(buf);
            if (n == 0) {
                basic_print("I2C1: NO DEVICE RESPONDED\n");
            } else {
                basic_print("I2C1:");
                for (int i = 0; i < n && i < 16; i++) {
                    snprintf(buf, sizeof(buf), " %02X", found[i]);
                    basic_print(buf);
                }
                basic_print("\n");
            }
            return;
        }
        // 再初期化で復帰した場合はそのまま値を表示する
    }
    snprintf(buf, sizeof(buf), "ACCEL %6d %6d %6d mG\n",
             hal_imu_accel_mg(0), hal_imu_accel_mg(1), hal_imu_accel_mg(2));
    basic_print(buf);
    snprintf(buf, sizeof(buf), "GYRO  %6d %6d %6d dps\n",
             hal_imu_gyro_dps(0), hal_imu_gyro_dps(1), hal_imu_gyro_dps(2));
    basic_print(buf);
}

// 電源を切る。BASIC の POWEROFF と、電源ボタンの長押しの両方から呼ぶ。
//
// 電池パスを閉じる前に、開いているファイルを閉じて書き込みを確定させる。
// 表示も出し切ってから切らないと、SPI 転送の途中で電源が消える。
//
// 電源が落ちればこの関数からは戻らない。戻ってきたのは切れなかったとき
// （USB 給電中か、ボタンを押したまま）で、そのときは実行中のプログラムを
// END と同じ形で止める。ファイルを閉じてしまった以上、閉じたまま次の文へ
// 進ませると、そこから「File not open」が出るか書いたつもりの出力が落ちる。
void basic_power_off() {
    basic_files_close_all();
    hal_display_set_deferred(false); // SYNC OFF のままだと表示が出ない
    basic_print("POWER OFF\n");
    hal_system_wait(300);            // 画面と USB へ出し切る猶予
    hal_battery_power_off();

    // ここに来たということは電源が切れていない。
    // USB 給電中か、電源ボタンを押したままか（ボタンは離すまでゲートを引き続ける）
    basic_print("STILL POWERED (USB OR KEY HELD)\n");

    // END と同じ止め方。ダイレクトモードでは current_line は元から -1
    current_line = -1;
    branch_taken = true;
}

void execute_poweroff(const TokenList& tokens, int& pos) {
    basic_power_off();  // 電源が落ちればここから戻らない
    pos = tokens.size;  // 戻ってきた＝切れなかった。同じ行の残りも実行しない
}

// SYNC / SYNC OFF / SYNC ON — 画面転送の制御。
//
// 既定（SYNC ON）は描画のたびに転送するので、「消してから描き直す」書き方だと
// 消えた状態が一瞬見えてちらつく。SYNC OFF でためておき、1 コマ分を描き終えてから
// SYNC でまとめて出すとちらつかない。
void execute_sync(const TokenList& tokens, int& pos) {
    pos++; // SYNC
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::ON) {
        pos++;
        hal_display_set_deferred(false); // ためた分を出してから通常動作へ
    } else if (pos < tokens.size && tokens.tokens[pos].type == TokenType::IDENTIFIER &&
               strcmp(tokens.tokens[pos].text, "OFF") == 0) {
        pos++;
        hal_display_set_deferred(true);
    } else {
        hal_display_flush(); // 引数なし: ためた分を今すぐ転送する
    }
}

// 乱数の種を作る。
//
// 従来の BASIC は明示的に RANDOMIZE を書かないと毎回同じ乱数列になるが、
// それだと電源を入れ直すたびにゲームの展開が固定されてしまう。本実装は
// RUN のたびにここで種を撒き直す（再現したいときは RANDOMIZE n / RND(-n)）。
unsigned int basic_random_seed_source() {
    // ユーザーが RUN を打つ時刻は毎回ずれるので、起動からの経過 ms だけでも
    // 実用上はばらける
    unsigned int seed = (unsigned int)hal_system_millis();

    // RTC が動いていれば日時も混ぜる。電源投入直後に自動実行するような
    // 使い方でも、日付が違えば違う列になる
    RtcTime t;
    if (hal_rtc_get(&t)) {
        unsigned int stamp = (unsigned int)(((t.year * 12 + t.month) * 31 + t.day) * 86400
                                            + t.hour * 3600 + t.minute * 60 + t.second);
        seed ^= stamp * 2654435761u; // Knuth の乗数で下位ビットまで散らす
    }
    return seed ? seed : 1u; // 0 は種として避ける
}

// RANDOMIZE [式] — 引数なしなら時刻から、あれば指定値で種を撒く
void execute_randomize(const TokenList& tokens, int& pos) {
    pos++; // RANDOMIZE
    bool has_arg = pos < tokens.size &&
                   tokens.tokens[pos].type != TokenType::END_OF_FILE &&
                   tokens.tokens[pos].type != TokenType::COLON &&
                   tokens.tokens[pos].type != TokenType::REM &&
                   tokens.tokens[pos].type != TokenType::ELSE;
    unsigned int seed;
    if (has_arg) {
        Value v = parse_relation(tokens, pos);
        if (!v.is_numeric())
            throw std::runtime_error("Type Mismatch: RANDOMIZE needs a number");
        seed = (unsigned int)(long)v.num_val;
    } else {
        seed = basic_random_seed_source();
    }
    srand(seed);
    last_rnd_val = 0.5f; // RND(0) が前回の種の値を返さないようにする
}

// 引数なしの `RTC` — 日付・時刻と状態をまとめて表示する
void execute_rtc_status(const TokenList&, int& pos) {
    pos++; // RTC
    RtcTime t;
    if (!hal_rtc_present() || !hal_rtc_get(&t)) {
        basic_print("RTC NOT FOUND\n");
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d\n",
             t.year, t.month, t.day, t.hour, t.minute, t.second);
    basic_print(buf);
    // OS フラグが立っていると、電源断などで時刻が飛んでいる
    basic_print(t.valid ? "CLOCK OK\n" : "CLOCK NOT SET\n");
}

// TIME$ = "HH:MM:SS" / DATE$ = "YYYY-MM-DD"
void execute_rtc_set(const TokenList& tokens, int& pos) {
    bool is_time = (strcmp(tokens.tokens[pos].text, "TIME$") == 0);
    pos++;
    require_token(tokens, pos, TokenType::ASSIGN, "Syntax Error: Expected '=' after TIME$/DATE$");
    pos++;
    Value v = parse_relation(tokens, pos);
    if (v.type != Value::Type::STR)
        throw std::runtime_error("Type Mismatch: TIME$/DATE$ needs a string");

    RtcTime t;
    if (!hal_rtc_get(&t)) throw std::runtime_error("RTC not found");
    if (!t.valid) {
        // 時刻が飛んでいる状態では、指定しなかった側が不定値のままになる。
        // 先に既定値へ寄せてから片方だけ上書きする
        t.year = 2000; t.month = 1; t.day = 1;
        t.hour = 0; t.minute = 0; t.second = 0;
    }

    // 弾いた理由を書式・範囲・暦で分ける。
    // 「2025-02-29」は書式も年も正しく、存在しない日付なのが理由なので、
    // 一律に書式のエラーを出すと原因を取り違えさせる
    if (is_time) {
        int hh, mm, ss;
        if (!rtc_time_fields(v.str_val, &hh, &mm, &ss))
            throw std::runtime_error("TIME$ must be \"HH:MM:SS\"");
        if (!rtc_parse_time(v.str_val, &t.hour, &t.minute, &t.second))
            throw std::runtime_error("Time out of range (00:00:00 to 23:59:59)");
    } else {
        int yy, mo, dd;
        if (!rtc_date_fields(v.str_val, &yy, &mo, &dd))
            throw std::runtime_error("DATE$ must be \"YYYY-MM-DD\"");
        if (yy < 2000 || yy > 2099)
            throw std::runtime_error("Year must be 2000 to 2099");
        if (!rtc_date_exists(yy, mo, dd))
            throw std::runtime_error("No such date");
        t.year = yy; t.month = mo; t.day = dd;
    }
    if (!hal_rtc_set(&t)) throw std::runtime_error("RTC write failed");
}

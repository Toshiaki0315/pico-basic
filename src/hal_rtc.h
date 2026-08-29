#pragma once

/// @file hal_rtc.h
/// リアルタイムクロック（PCF85063A）と、日時文字列の解釈。
#include <stdbool.h>

// ---------------------------------------------------------
// PCF85063ATL リアルタイムクロック
// Waveshare RP2350-Touch-LCD-2.8 の U5
//
// 回路図より:
//   SDA/SCL (6/7)  -> GP6/GP7 … タッチ(0x1A)・IMU(0x6B) と共用の i2c1
//   INT     (4)    -> GP5     … 本実装では未使用
//   OSCI/OSCO      -> 32.768kHz 水晶 Y2（+ C22/C29 22pF）
//   VDD            -> 3V3（D4 経由）。J2 にバックアップ電池を繋げば
//                     本体の電源を切っても時刻を保持する
//
// I2C アドレスは 0x51 固定（実機のバス走査でも 0x51 が応答）。
// レジスタはデータシート PCF85063A Rev 7.3 の Table 4 に準拠:
//   00h Control_1（bit5 STOP / bit1 12_24 / bit0 CAP_SEL）
//   04h Seconds（bit7 = OS: 立っていると時刻が信用できない）… 0Ah Years
//   時刻・日付はすべて BCD。年はチップ側が 00-99 しか持てない
// ---------------------------------------------------------

struct RtcTime {
    int  year;   // 西暦 2000-2099
    int  month;  // 1-12
    int  day;    // 1-31
    int  hour;   // 0-23
    int  minute; // 0-59
    int  second; // 0-59
    bool valid;  // false = 電源断などで時刻が信用できない（OS フラグ）
};

/// @brief RTC を初期化する。見つからなければ以降 false を返し続ける
void hal_rtc_init();

// RTC が応答しているか
/**
 * @brief RTC が載っているか。
 * @return 見つかっていれば true
 */
bool hal_rtc_present();

/**
 * @brief 現在時刻を読む。
 * @param[out] out 読み取った日時
 * @return 読めたら true
 */
bool hal_rtc_get(RtcTime* out);
/**
 * @brief 時刻を設定する。
 * @param t 設定する日時
 * @return 書けたら true
 */
bool hal_rtc_set(const RtcTime* t);

// ホストテスト用: 実機ビルドでは何もしない
/// @brief テスト用の差し込み口（実機では何もしない）
/**
 * @brief テスト用の差し込み口（実機では何もしない）。
 * @param t 以降 hal_rtc_get() が返す日時
 */
void hal_rtc_set_mock(const RtcTime* t);
/**
 * @brief テスト用の差し込み口（実機では何もしない）。
 * @param present 以降 hal_rtc_present() が返す値
 */
void hal_rtc_set_mock_present(bool present);

// ---------------------------------------------------------
// ハードウェアに依存しない変換・検証（ホストテストで確認する）
// ---------------------------------------------------------

/**
 * @brief BCD を整数へ直す。
 * @param b BCD の 1 バイト
 * @return 整数値
 */
inline int bcd_to_int(unsigned char b) {
    return (b >> 4) * 10 + (b & 0x0F);
}

/**
 * @brief 整数を BCD へ直す。
 * @param v 0-99
 * @return BCD の 1 バイト
 */
inline unsigned char int_to_bcd(int v) {
    return (unsigned char)(((v / 10) << 4) | (v % 10));
}

/**
 * @brief 閏年か。
 * @param y 西暦
 * @return 閏年なら true
 */
inline bool rtc_is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

/**
 * @brief その月の日数。
 * @param y 西暦
 * @param m 月（1-12）
 * @return 日数。月が範囲外なら 0
 */
inline int rtc_days_in_month(int y, int m) {
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 0;
    if (m == 2 && rtc_is_leap(y)) return 29;
    return days[m - 1];
}

// 曜日（0=日曜）。Sakamoto の方法。
// チップの Weekdays レジスタを正しく埋めるために使う
/**
 * @brief 曜日を求める。
 * @param y,m,d 西暦・月・日
 * @return 0=日曜 … 6=土曜
 */
inline int rtc_weekday(int y, int m, int d) {
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// "HH:MM:SS" の書式だけを見て数値を取り出す（範囲は見ない）。
// 「書式が違う」のか「値が範囲外」なのかをエラーで区別するために分けてある。
/**
 * @brief "HH:MM:SS" を数値に分解する（範囲は見ない）。
 * @param s 時刻文字列
 * @param[out] hour,minute,second 分解した値
 * @return 形式が合っていれば true
 */
inline bool rtc_time_fields(const char* s, int* hour, int* minute, int* second) {
    if (!s) return false;
    int n = 0;
    while (s[n]) n++;
    if (n != 8 || s[2] != ':' || s[5] != ':') return false;
    for (int i = 0; i < 8; i++) {
        if (i == 2 || i == 5) continue;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    *hour   = (s[0] - '0') * 10 + (s[1] - '0');
    *minute = (s[3] - '0') * 10 + (s[4] - '0');
    *second = (s[6] - '0') * 10 + (s[7] - '0');
    return true;
}

// "HH:MM:SS" を解釈する。桁数・区切り・範囲に厳密。
/**
 * @brief "HH:MM:SS" を解釈する。
 * @param s 時刻文字列
 * @param[out] hour,minute,second 解釈した値
 * @return 形式も範囲も正しければ true
 */
inline bool rtc_parse_time(const char* s, int* hour, int* minute, int* second) {
    int hh, mm, ss;
    if (!rtc_time_fields(s, &hh, &mm, &ss)) return false;
    if (hh > 23 || mm > 59 || ss > 59) return false;
    *hour = hh; *minute = mm; *second = ss;
    return true;
}

// "YYYY-MM-DD" の書式だけを見て数値を取り出す（暦の妥当性は見ない）。
/**
 * @brief "YYYY-MM-DD" を数値に分解する（実在するかは見ない）。
 * @param s 日付文字列
 * @param[out] year,month,day 分解した値
 * @return 形式が合っていれば true
 */
inline bool rtc_date_fields(const char* s, int* year, int* month, int* day) {
    if (!s) return false;
    int n = 0;
    while (s[n]) n++;
    if (n != 10 || s[4] != '-' || s[7] != '-') return false;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) continue;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    *year  = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    *month = (s[5] - '0') * 10 + (s[6] - '0');
    *day   = (s[8] - '0') * 10 + (s[9] - '0');
    return true;
}

// 実在する日付か（年の範囲は見ない）
/**
 * @brief その日付が実在するか（閏年も見る）。
 * @param year,month,day 西暦・月・日
 * @return 実在すれば true
 */
inline bool rtc_date_exists(int year, int month, int day) {
    if (month < 1 || month > 12) return false;
    return day >= 1 && day <= rtc_days_in_month(year, month);
}

// "YYYY-MM-DD" を解釈する。年は 2000-2099（チップが 2 桁しか持てないため）。
/**
 * @brief "YYYY-MM-DD" を解釈する。
 * @param s 日付文字列
 * @param[out] year,month,day 解釈した値
 * @return 実在する日付なら true（閏年も見る）
 */
inline bool rtc_parse_date(const char* s, int* year, int* month, int* day) {
    int yy, mo, dd;
    if (!rtc_date_fields(s, &yy, &mo, &dd)) return false;
    if (yy < 2000 || yy > 2099) return false;
    if (!rtc_date_exists(yy, mo, dd)) return false;
    *year = yy; *month = mo; *day = dd;
    return true;
}

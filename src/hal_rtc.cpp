#include "hal_rtc.h"
#include <stdio.h>

#if __has_include("pico/stdlib.h")
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "board_config.h"

#define RTC_I2C       BOARD_I2C
#define RTC_SDA_PIN   BOARD_I2C_SDA
#define RTC_SCL_PIN   BOARD_I2C_SCL
#define PCF85063_ADDR 0x51

// レジスタ（PCF85063A データシート Rev 7.3 Table 4）
#define REG_CONTROL_1 0x00
#define REG_SECONDS   0x04 // ここから 秒/分/時/日/曜/月/年 が 7 バイト並ぶ

#define CTRL1_STOP  0x20 // 立てるとカウンタが止まる
#define CTRL1_12_24 0x02 // 0 = 24 時間モード（既定）

#define SECONDS_OS  0x80 // 発振停止フラグ。立っていると時刻が信用できない

static bool rtc_ok = false;

static bool rtc_read(uint8_t reg, uint8_t* buf, size_t len) {
    if (i2c_write_blocking(RTC_I2C, PCF85063_ADDR, &reg, 1, true) != 1) return false;
    return i2c_read_blocking(RTC_I2C, PCF85063_ADDR, buf, len, false) == (int)len;
}

static bool rtc_write(uint8_t reg, const uint8_t* data, size_t len) {
    uint8_t buf[9];
    if (len + 1 > sizeof(buf)) return false;
    buf[0] = reg;
    for (size_t i = 0; i < len; i++) buf[i + 1] = data[i];
    return i2c_write_blocking(RTC_I2C, PCF85063_ADDR, buf, len + 1, false) == (int)(len + 1);
}

void hal_rtc_init() {
    rtc_ok = false;

    // タッチ・IMU と同じバス。単体でも動くよう同じ設定で初期化しておく
    i2c_init(RTC_I2C, BOARD_I2C_BAUD);
    gpio_set_function(RTC_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(RTC_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(RTC_SDA_PIN);
    gpio_pull_up(RTC_SCL_PIN);

    // PCF85063A には ID レジスタが無いので、Control_1 が読めるかで在否を見る
    uint8_t ctrl1 = 0;
    if (!rtc_read(REG_CONTROL_1, &ctrl1, 1)) {
        printf("[RTC] PCF85063A not found\n");
        return;
    }

    // カウンタを動かし、24 時間モードにする。
    // CAP_SEL(bit0) は水晶の負荷容量の設定なので触らずに残す
    uint8_t want = (uint8_t)(ctrl1 & ~(CTRL1_STOP | CTRL1_12_24));
    if (want != ctrl1) {
        if (!rtc_write(REG_CONTROL_1, &want, 1)) return;
    }

    rtc_ok = true;
    printf("[RTC] PCF85063A at 0x%02X\n", PCF85063_ADDR);
}

bool hal_rtc_present() { return rtc_ok; }

bool hal_rtc_get(RtcTime* out) {
    if (!rtc_ok || !out) return false;

    uint8_t b[7];
    if (!rtc_read(REG_SECONDS, b, sizeof(b))) return false;

    // 上位の未使用ビットはマスクしてから BCD を解く
    out->valid  = (b[0] & SECONDS_OS) == 0;
    out->second = bcd_to_int(b[0] & 0x7F);
    out->minute = bcd_to_int(b[1] & 0x7F);
    out->hour   = bcd_to_int(b[2] & 0x3F);
    out->day    = bcd_to_int(b[3] & 0x3F);
    // b[4] は曜日。年月日から導けるので読み捨てる
    out->month  = bcd_to_int(b[5] & 0x1F);
    out->year   = 2000 + bcd_to_int(b[6]);
    return true;
}

bool hal_rtc_set(const RtcTime* t) {
    if (!rtc_ok || !t) return false;

    uint8_t b[7];
    // 秒の bit7 に 0 を書くと OS フラグが落ちる（＝時刻を信用してよい状態になる）
    b[0] = (uint8_t)(int_to_bcd(t->second) & 0x7F);
    b[1] = int_to_bcd(t->minute);
    b[2] = int_to_bcd(t->hour);
    b[3] = int_to_bcd(t->day);
    b[4] = (uint8_t)rtc_weekday(t->year, t->month, t->day);
    b[5] = int_to_bcd(t->month);
    b[6] = int_to_bcd(t->year - 2000);
    return rtc_write(REG_SECONDS, b, sizeof(b));
}

void hal_rtc_set_mock(const RtcTime*) {
    // 実機では何もしない（テスト専用の入口）
}
void hal_rtc_set_mock_present(bool) {}

#else
// ---------------------------------------------------------
// ホストビルド用。テストから値を差し込めるようにしておく
// ---------------------------------------------------------
static RtcTime mock_time;
static bool    mock_present;

void hal_rtc_init() {
    mock_time = RtcTime{2026, 8, 1, 12, 0, 0, true};
    mock_present = true;
}

bool hal_rtc_present() { return mock_present; }

bool hal_rtc_get(RtcTime* out) {
    if (!mock_present || !out) return false;
    *out = mock_time;
    return true;
}

bool hal_rtc_set(const RtcTime* t) {
    if (!mock_present || !t) return false;
    mock_time = *t;
    mock_time.valid = true; // 実機と同じく、書き込むと OS フラグが落ちる
    return true;
}

void hal_rtc_set_mock(const RtcTime* t) { if (t) mock_time = *t; }
void hal_rtc_set_mock_present(bool present) { mock_present = present; }

#endif

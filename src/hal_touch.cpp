#include "hal_touch.h"
#include <stdio.h>

#if __has_include("pico/stdlib.h")
// ---------------------------------------------------------
// Pico Target: CST328 capacitive touch controller (I2C)
//
// 配線（公式回路図のネットリストで確認）:
//   SDA = GP6 / SCL = GP7 … i2c1。IMU(QMI8658) や RTC と共用のバス
//   RST = GP17
//   INT = GP18
//
// レジスタ仕様は Waveshare 公式デモの bsp_cst328.c に準拠。
// アドレスは 16bit（ビッグエンディアン）で指定する。
// ---------------------------------------------------------
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "board_config.h"

// 配線は board_config.h に集約（IMU・RTC と同じバスを共用する）
#define TP_I2C        BOARD_I2C
#define TP_SDA_PIN    BOARD_I2C_SDA
#define TP_SCL_PIN    BOARD_I2C_SCL
#define TP_RST_PIN    BOARD_TP_RST
#define TP_INT_PIN    BOARD_TP_INT

#define CST328_ADDR   0x1A

#define CST328_REG_FIRST_TOUCH       0xD000 // 座標データ（1点あたり5バイト）
#define CST328_REG_TOUCH_FLAG_NUM    0xD005 // 下位4bitがタッチ点数
#define CST328_REG_MODE_DEBUG_INFO   0xD101
#define CST328_REG_MODE_NORMAL       0xD109 // 通常の報告モード
#define CST328_REG_IC_INFO           0xD1F4

// hal_display のフレームバッファに合わせた画面サイズ（横向き）
#define SCREEN_WIDTH  BOARD_LCD_WIDTH
#define SCREEN_HEIGHT BOARD_LCD_HEIGHT

// 同じフレーム内で TOUCH(0)/TOUCH(1)/TOUCH(2) を続けて読んでも
// 値がずれないよう、この間隔でだけ実機を読みに行く
#define POLL_INTERVAL_MS 10

static bool controller_ok = false;
static int  touch_state   = 0;
static int  touch_x       = 0;
static int  touch_y       = 0;
static bool     polled_once  = false;
static uint32_t last_poll_ms = 0;

static bool cst328_read_reg(uint16_t reg, uint8_t* buf, size_t len) {
    uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    if (i2c_write_blocking(TP_I2C, CST328_ADDR, addr, 2, true) != 2) return false;
    return i2c_read_blocking(TP_I2C, CST328_ADDR, buf, len, false) == (int)len;
}

// データを伴わないコマンド（モード切替）
static bool cst328_write_cmd(uint16_t reg) {
    uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_write_blocking(TP_I2C, CST328_ADDR, addr, 2, false) == 2;
}

static void cst328_reset() {
    gpio_put(TP_RST_PIN, 0);
    sleep_ms(10);
    gpio_put(TP_RST_PIN, 1);
    sleep_ms(50);
}

void hal_touch_init() {
    touch_state = 0;
    touch_x     = 0;
    touch_y     = 0;

    i2c_init(TP_I2C, BOARD_I2C_BAUD);
    gpio_set_function(TP_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(TP_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(TP_SDA_PIN);
    gpio_pull_up(TP_SCL_PIN);

    gpio_init(TP_RST_PIN);
    gpio_set_dir(TP_RST_PIN, GPIO_OUT);
    cst328_reset();

    // INT は使わずポーリングで読むが、入力に固定しておく
    gpio_init(TP_INT_PIN);
    gpio_set_dir(TP_INT_PIN, GPIO_IN);
    gpio_pull_up(TP_INT_PIN);

    // 情報モードに入って識別コードを確認する
    uint8_t info[24] = {0};
    cst328_write_cmd(CST328_REG_MODE_DEBUG_INFO);
    if (cst328_read_reg(CST328_REG_IC_INFO, info, sizeof(info))) {
        uint16_t check_code = ((uint16_t)info[11] << 8) | info[10];
        controller_ok = (check_code == 0xCACA);
    }

    // 通常の報告モードへ戻す（識別に失敗していても送っておく）
    cst328_write_cmd(CST328_REG_MODE_NORMAL);

    if (!controller_ok) {
        printf("[TP] CST328 not found (TOUCH() will always return 0)\n");
    }
}

static void poll_touch() {
    if (!controller_ok) return;

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (polled_once && (now - last_poll_ms) < POLL_INTERVAL_MS) return;
    polled_once  = true;
    last_poll_ms = now;

    uint8_t flag = 0;
    if (!cst328_read_reg(CST328_REG_TOUCH_FLAG_NUM, &flag, 1)) {
        touch_state = 0;
        return;
    }
    if ((flag & 0x0F) == 0) { // 触れていない
        touch_state = 0;
        return;
    }

    // 1点目だけ使う。5点分（各5バイト）+ 先頭のIDバイトで 27 バイト
    uint8_t data[27];
    if (!cst328_read_reg(CST328_REG_FIRST_TOUCH, data, sizeof(data))) {
        touch_state = 0;
        return;
    }
    if ((data[0] & 0x0F) != 0x06) { // 有効な報告ではない
        touch_state = 0;
        return;
    }

    // パネル本来の向き（縦 240x320）での座標
    uint16_t raw_x = ((uint16_t)data[1] << 4) | ((data[3] & 0xF0) >> 4);
    uint16_t raw_y = ((uint16_t)data[2] << 4) | (data[3] & 0x0F);

    // hal_display は MADCTL=0x70（公式デモの rotation 1 相当）で横向きに
    // 使っているので、それに合わせて 320x240 の画面座標へ変換する
    int x = (int)raw_y;
    int y = (SCREEN_HEIGHT - 1) - (int)raw_x;

    if (x < 0) x = 0;
    if (x > SCREEN_WIDTH - 1)  x = SCREEN_WIDTH - 1;
    if (y < 0) y = 0;
    if (y > SCREEN_HEIGHT - 1) y = SCREEN_HEIGHT - 1;

    touch_x = x;
    touch_y = y;
    touch_state = 1;
}

int hal_touch_is_touched() {
    poll_touch();
    return touch_state;
}

int hal_touch_get_x() {
    poll_touch();
    return touch_x;
}

int hal_touch_get_y() {
    poll_touch();
    return touch_y;
}

#else
// ---------------------------------------------------------
// Host/Stub implementation of Touch HAL
// ---------------------------------------------------------

static int touch_state  = 0;
static int touch_x      = 0;
static int touch_y      = 0;

void hal_touch_init() {
    touch_state = 0;
    touch_x     = 0;
    touch_y     = 0;
}

int hal_touch_is_touched() {
    return touch_state;
}

int hal_touch_get_x() {
    return touch_x;
}

int hal_touch_get_y() {
    return touch_y;
}

#endif

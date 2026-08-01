#include "hal_imu.h"
#include <stdio.h>

#if __has_include("pico/stdlib.h")
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define IMU_I2C      i2c1
#define IMU_SDA_PIN  6
#define IMU_SCL_PIN  7
#define QMI8658_ADDR 0x6A

// レジスタ（QMI8658C データシート Rev 0.6 §4.1）
#define REG_WHO_AM_I 0x00
#define REG_CTRL1    0x02
#define REG_CTRL2    0x03
#define REG_CTRL3    0x04
#define REG_CTRL7    0x08
#define REG_AX_L     0x35 // ここから AX/AY/AZ/GX/GY/GZ が 12 バイト並ぶ

#define WHO_AM_I_VALUE 0x05

// CTRL1: bit6 SPI_AI（アドレス自動インクリメント）を立てる。
// 6 軸を 1 回の転送でまとめて読むために必須。bit5 SPI_BE は既定値の 1 を残す
#define CTRL1_VALUE 0x60
// CTRL2: 加速度 aFS=001(±4g), aODR=0101(250Hz)
#define CTRL2_VALUE 0x15
// CTRL3: ジャイロ gFS=101(±512dps), gODR=0101(250Hz)
#define CTRL3_VALUE 0x55
// CTRL7: aEN | gEN（磁気と AttitudeEngine は使わない）
#define CTRL7_VALUE 0x03

// 同じフレーム内で ACCEL(0)/ACCEL(1)/ACCEL(2) を続けて読んだときに、
// 別々のサンプルが混ざって傾きの計算が狂わないよう、この間隔でだけ読みに行く。
// ODR は 250Hz（4ms ごとに更新）なので 5ms なら鮮度も保てる
#define POLL_INTERVAL_MS 5

static bool     imu_ok      = false;
static int16_t  raw[6]      = {0, 0, 0, 0, 0, 0};
static bool     polled_once = false;
static uint32_t last_poll_ms = 0;

static bool imu_write_reg(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    return i2c_write_blocking(IMU_I2C, QMI8658_ADDR, buf, 2, false) == 2;
}

static bool imu_read_reg(uint8_t reg, uint8_t* buf, size_t len) {
    if (i2c_write_blocking(IMU_I2C, QMI8658_ADDR, &reg, 1, true) != 1) return false;
    return i2c_read_blocking(IMU_I2C, QMI8658_ADDR, buf, len, false) == (int)len;
}

void hal_imu_init() {
    imu_ok      = false;
    polled_once = false;

    // タッチと同じバス。hal_touch_init() が先に済ませている想定だが、
    // 単体でも動くよう同じ設定で初期化しておく（値が同じなので害はない）
    i2c_init(IMU_I2C, 400 * 1000);
    gpio_set_function(IMU_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(IMU_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(IMU_SDA_PIN);
    gpio_pull_up(IMU_SCL_PIN);

    uint8_t who = 0;
    if (!imu_read_reg(REG_WHO_AM_I, &who, 1) || who != WHO_AM_I_VALUE) {
        printf("[IMU] QMI8658 not found (WHO_AM_I=0x%02X)\n", who);
        return;
    }

    if (!imu_write_reg(REG_CTRL1, CTRL1_VALUE)) return;
    if (!imu_write_reg(REG_CTRL2, CTRL2_VALUE)) return;
    if (!imu_write_reg(REG_CTRL3, CTRL3_VALUE)) return;
    if (!imu_write_reg(REG_CTRL7, CTRL7_VALUE)) return;

    imu_ok = true;
}

static void imu_poll() {
    if (!imu_ok) return;

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (polled_once && (now - last_poll_ms) < POLL_INTERVAL_MS) return;

    uint8_t b[12];
    if (!imu_read_reg(REG_AX_L, b, sizeof(b))) return;

    // 各軸はリトルエンディアンの 16bit 2 の補数
    for (int i = 0; i < 6; i++) {
        raw[i] = (int16_t)((uint16_t)b[i * 2] | ((uint16_t)b[i * 2 + 1] << 8));
    }
    last_poll_ms = now;
    polled_once  = true;
}

bool hal_imu_present() { return imu_ok; }

int hal_imu_accel_mg(int axis) {
    if (axis < 0 || axis > 2) return 0;
    imu_poll();
    return imu_raw_to_mg(raw[axis]);
}

int hal_imu_gyro_dps(int axis) {
    if (axis < 0 || axis > 2) return 0;
    imu_poll();
    return imu_raw_to_dps(raw[3 + axis]);
}

int hal_imu_diagnose(unsigned char* found, int max_found, unsigned char* who) {
    // 起動直後に間に合わなかっただけなら、これで拾えることがある
    hal_imu_init();

    uint8_t w = 0;
    imu_read_reg(REG_WHO_AM_I, &w, 1);
    if (who) *who = w;

    // i2c1 に応答するアドレスを列挙する。1 バイト読んで ACK が返るかで判定
    int n = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        uint8_t dummy;
        if (i2c_read_blocking(IMU_I2C, addr, &dummy, 1, false) >= 0) {
            if (n < max_found) found[n] = addr;
            n++;
        }
    }
    return n;
}

void hal_imu_set_mock(int, int, int, int, int, int) {
    // 実機では何もしない（テスト専用の入口）
}
void hal_imu_set_mock_present(bool) {}

#else
// ---------------------------------------------------------
// ホストビルド用。テストから値を差し込めるようにしておく
// ---------------------------------------------------------
static int  mock_accel[3];
static int  mock_gyro[3];
static bool mock_present;

void hal_imu_init() {
    for (int i = 0; i < 3; i++) { mock_accel[i] = 0; mock_gyro[i] = 0; }
    mock_present = true;
}

bool hal_imu_present() { return mock_present; }

int hal_imu_accel_mg(int axis) {
    if (axis < 0 || axis > 2) return 0;
    return mock_accel[axis];
}

int hal_imu_gyro_dps(int axis) {
    if (axis < 0 || axis > 2) return 0;
    return mock_gyro[axis];
}

int hal_imu_diagnose(unsigned char*, int, unsigned char* who) {
    if (who) *who = mock_present ? 0x05 : 0x00;
    return 0; // ホストには I2C バスが無い
}

void hal_imu_set_mock(int ax, int ay, int az, int gx, int gy, int gz) {
    mock_accel[0] = ax; mock_accel[1] = ay; mock_accel[2] = az;
    mock_gyro[0]  = gx; mock_gyro[1]  = gy; mock_gyro[2]  = gz;
}
void hal_imu_set_mock_present(bool present) { mock_present = present; }

#endif

#pragma once
#include <stdbool.h>

// ---------------------------------------------------------
// QMI8658（6軸 IMU: 3軸加速度 + 3軸ジャイロ）
// Waveshare RP2350-Touch-LCD-2.8 の U4
//
// 回路図より:
//   SDO/SA0 (pin 1)  -> GND   … I2C アドレスの最下位ビット = 0 なので 0x6A
//   CS      (pin 12) -> 3V3   … I2C モード固定（SPI ではない）
//   SDA/SCL (14/13)  -> GP6/GP7 … タッチ(CST328 0x1A)・RTC(PCF85063 0x51) と
//                                  共用の i2c1。アドレスが違うので競合しない
//   INT1/INT2 (4/9)  -> GP8/GP9 … 本実装では未使用（ポーリングで読む）
//
// 設定はデータシート QMI8658C Rev 0.6 の §4 レジスタマップに準拠。
// ---------------------------------------------------------

void hal_imu_init();

// IMU が応答しているか（WHO_AM_I が読めたか）
bool hal_imu_present();

// 加速度をミリ G で返す（1G = 1000）。axis: 0=X / 1=Y / 2=Z
int hal_imu_accel_mg(int axis);

// 角速度を度/秒で返す。axis: 0=X / 1=Y / 2=Z
int hal_imu_gyro_dps(int axis);

// 見つからないときの切り分け用。
//  1. 初期化をやり直す（起動直後に間に合わなかった場合はこれで復帰する）
//  2. WHO_AM_I の生の値を who に入れる（期待値 0x05）
//  3. i2c1 で応答するアドレスを found に列挙する
// 戻り値は応答したデバイスの数。
int hal_imu_diagnose(unsigned char* found, int max_found, unsigned char* who);

// ホストテスト用: 実機ビルドでは何もしない
void hal_imu_set_mock(int ax, int ay, int az, int gx, int gy, int gz);
void hal_imu_set_mock_present(bool present);

// ---------------------------------------------------------
// 生の値の換算（ハードウェアに依存しない計算）
// データシートの感度表より:
//   加速度 フルスケール ±4g   → 8,192 LSB/g
//   ジャイロ フルスケール ±512dps →    64 LSB/dps
// ---------------------------------------------------------

inline int imu_raw_to_mg(int raw) {
    // raw は最大 32767 なので *1000 しても int に収まる
    return raw * 1000 / 8192;
}

inline int imu_raw_to_dps(int raw) {
    return raw / 64;
}

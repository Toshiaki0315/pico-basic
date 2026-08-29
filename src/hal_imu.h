#pragma once

/// @file hal_imu.h
/// 加速度・角速度センサー（BASIC の ACCEL / GYRO 関数）。
#include <stdbool.h>

// ---------------------------------------------------------
// QMI8658（6軸 IMU: 3軸加速度 + 3軸ジャイロ）
// Waveshare RP2350-Touch-LCD-2.8 の U4
//
// 回路図より:
//   SDO/SA0 (pin 1)  -> アドレスの最下位ビットを決める。0x6A(Low) か 0x6B(High)。
//                       図では GND に見えたが実機は 0x6B で応答したため、
//                       初期化で両方を試して当たった方を使う
//   CS      (pin 12) -> 3V3   … I2C モード固定（SPI ではない）
//   SDA/SCL (14/13)  -> GP6/GP7 … タッチ(CST328 0x1A)・RTC(PCF85063 0x51) と
//                                  共用の i2c1。アドレスが違うので競合しない
//   INT1/INT2 (4/9)  -> GP8/GP9 … 本実装では未使用（ポーリングで読む）
//
// 設定はデータシート QMI8658C Rev 0.6 の §4 レジスタマップに準拠。
// ---------------------------------------------------------

/// @brief IMU を初期化する。見つからなければ以降 0 を返し続ける
void hal_imu_init();

// IMU が応答しているか（WHO_AM_I が読めたか）
/**
 * @brief IMU が応答しているか（WHO_AM_I が読めたか）。
 * @return 見つかっていれば true
 */
bool hal_imu_present();

// 加速度をミリ G で返す（1G = 1000）。axis: 0=X / 1=Y / 2=Z
/**
 * @brief 加速度を読む。
 * @param axis 0=X / 1=Y / 2=Z
 * @return mg 単位（1G = 1000）。IMU が無ければ 0
 */
int hal_imu_accel_mg(int axis);

// 角速度を度/秒で返す。axis: 0=X / 1=Y / 2=Z
/**
 * @brief 角速度を読む。
 * @param axis 0=X / 1=Y / 2=Z
 * @return dps 単位（度/秒）。IMU が無ければ 0
 */
int hal_imu_gyro_dps(int axis);

// 実際に応答した I2C アドレス（0x6A か 0x6B）。未検出なら 0。
/**
 * @brief 実際に応答した I2C アドレス。
 * @return 0x6A か 0x6B。見つかっていなければ 0
 */
unsigned char hal_imu_address();

// 見つからないときの切り分け用。
//  1. 初期化をやり直す（起動直後に間に合わなかった場合はこれで復帰する）
//  2. WHO_AM_I の生の値を who に入れる（期待値 0x05）
//  3. i2c1 で応答するアドレスを found に列挙する
// 戻り値は応答したデバイスの数。
/**
 * @brief I2C バス上の応答を調べる（不具合調査用）。
 * @param[out] found 応答したアドレスの列
 * @param max_found found に入る個数
 * @param[out] who IMU の WHO_AM_I の値
 * @return found に入れた個数
 */
int hal_imu_diagnose(unsigned char* found, int max_found, unsigned char* who);

// ホストテスト用: 実機ビルドでは何もしない
/**
 * @brief テスト用の差し込み口（実機では何もしない）。
 * @param ax,ay,az 以降 hal_imu_accel_mg() が返す値
 * @param gx,gy,gz 以降 hal_imu_gyro_dps() が返す値
 */
void hal_imu_set_mock(int ax, int ay, int az, int gx, int gy, int gz);
/**
 * @brief テスト用の差し込み口（実機では何もしない）。
 * @param present 以降 hal_imu_present() が返す値
 */
void hal_imu_set_mock_present(bool present);

// ---------------------------------------------------------
// 生の値の換算（ハードウェアに依存しない計算）
// データシートの感度表より:
//   加速度 フルスケール ±4g   → 8,192 LSB/g
//   ジャイロ フルスケール ±512dps →    64 LSB/dps
// ---------------------------------------------------------

/**
 * @brief 加速度の生値を mg へ直す。
 * @param raw センサーの生値
 * @return mg 単位（1G = 1000）
 */
inline int imu_raw_to_mg(int raw) {
    // raw は最大 32767 なので *1000 しても int に収まる
    return raw * 1000 / 8192;
}

/**
 * @brief 角速度の生値を dps へ直す。
 * @param raw センサーの生値
 * @return dps 単位（度/秒）
 */
inline int imu_raw_to_dps(int raw) {
    return raw / 64;
}

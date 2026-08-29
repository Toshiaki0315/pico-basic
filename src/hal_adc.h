#pragma once

/// @file hal_adc.h
#include "board_config.h"

// 汎用アナログ入力と内蔵温度センサー（RP2350）
//
// ADIN(gpio) は拡張コネクタに出ているピンを 0-4095 の生値で読む。
// BAT_EN は電源ラッチの制御線で、アナログ入力に切り替えると出力が切れて
// 電池運用時にその場で電源が落ちるため、読める対象から外してある。

// ADC ブロックの初期化。何度呼んでも安全で、内蔵温度センサーの有効化まで含む。
//
// **adc_init() を直に呼ぶのはこの関数だけにすること。** adc_init() は ADC
// ブロックをリセットするので、あとから別の HAL がもう一度呼ぶと、先に有効化
// してあった温度センサーが黙って落ちる。各 HAL は自分の使うピンの
// adc_gpio_init() だけを持ち、ブロックの面倒はここに集約する。
/// @brief ADC ブロックを初期化する（何度呼んでも安全）
void hal_adc_block_init();

/// @brief ADIN で読む対象のピンをアナログ入力に切り替える。ブロックの初期化も含む
void hal_adc_init();

/**
 * @brief アナログ入力を読む（16 サンプルの平均）。
 * @param gpio GPIO 番号。adc_pin_allowed() が true を返すピンだけ
 * @return 0-4095 の生値。読めないピンなら -1
 */
int hal_adc_read_raw(int gpio);

/**
 * @brief 内蔵温度センサーを読む。
 * @return 摂氏。校正していないので数℃の個体差がある
 */
float hal_adc_read_temp_c();

/// @brief テスト用の差し込み口（実機では何もしない）
/**
 * @brief テスト用の差し込み口（実機では何もしない）。
 * @param gpio 値を差し込む GPIO 番号
 * @param raw 以降 hal_adc_read_raw() が返す値
 */
void hal_adc_set_mock_raw(int gpio, int raw);
/**
 * @brief テスト用の差し込み口（実機では何もしない）。
 * @param celsius 以降 hal_adc_read_temp_c() が返す値
 */
void hal_adc_set_mock_temp_c(float celsius);

// ---------------------------------------------------------
// ハードウェアに依存しない判定（ADIN 関数と共有する）
// ---------------------------------------------------------

// RP2350 の ADC は GPIO26..29 が入力 0..3 に順に対応し、入力 4 が内蔵温度センサー。
// これはチップ側の都合なので、基板の配線を書く board_config.h ではなくここに置く。
constexpr int ADC_GPIO_BASE    = 26;
constexpr int ADC_INPUT_COUNT  = 4;
constexpr int ADC_TEMP_INPUT   = 4;

// GPIO 番号 -> ADC 入力番号。ADC に繋がっていないピンなら -1。
/**
 * @brief GPIO 番号を ADC 入力番号へ直す。
 * @param gpio GPIO 番号
 * @return 入力番号（0-3）。ADC に繋がっていないピンなら -1
 */
constexpr int adc_input_for_gpio(int gpio) {
    int input = gpio - ADC_GPIO_BASE;
    return (input >= 0 && input < ADC_INPUT_COUNT) ? input : -1;
}

// ADIN で読んでよい GPIO か。BAT_EN は意図的に外してある（上のコメント参照）。
//
// 対象ピンは board_config.h の定義から決まるので、別のボードでもここは直さずに済む。
// ADC に繋がっているかどうかを先に見るのは、board_config.h が ADC の無いピンを
// 指していた場合に、そのまま入力番号として使われて範囲外に飛ぶのを止めるため。
/**
 * @brief ADIN で読んでよいピンか。
 * @param gpio GPIO 番号
 * @return 読んでよければ true
 */
inline bool adc_pin_allowed(int gpio) {
    if (adc_input_for_gpio(gpio) < 0) return false;
    return gpio == BOARD_BAT_ADC_GPIO ||
           gpio == BOARD_FREE_GPIO_A  ||
           gpio == BOARD_FREE_GPIO_B;
}

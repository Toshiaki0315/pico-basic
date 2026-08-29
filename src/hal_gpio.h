#pragma once
#include <stdint.h>
#include <stdbool.h>

/// @file hal_gpio.h
/// 汎用 GPIO の読み書き（BASIC の GPIO 文 / PIN 関数から使う）。

/**
 * @brief ピンの向きとプルを設定する。
 * @param pin GPIO 番号
 * @param mode 0=入力 / 1=出力
 * @param pull 0=なし / 1=プルアップ / 2=プルダウン
 */
void hal_gpio_init(int pin, int mode, int pull);

/**
 * @brief 出力ピンの値を書く。
 * @param pin GPIO 番号
 * @param value true で High、false で Low
 */
void hal_gpio_write(int pin, bool value);

/**
 * @brief ピンの値を読む。
 * @param pin GPIO 番号
 * @return High なら true
 */
bool hal_gpio_read(int pin);

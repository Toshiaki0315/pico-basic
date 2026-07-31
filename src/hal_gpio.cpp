#include "hal_gpio.h"

/* Pico SDK ビルドでは pico/stdlib.h が存在する。ホストテストでは mock が GPIO を提供 */
#if __has_include("pico/stdlib.h")
#include "pico/stdlib.h"
#include "hardware/gpio.h"

void hal_gpio_init(int pin, int mode, int pull) {
    gpio_init(pin);
    if (mode == 1) { // Output
        gpio_set_dir(pin, GPIO_OUT);
    } else { // Input
        gpio_set_dir(pin, GPIO_IN);
        if (pull == 1) gpio_pull_up(pin);
        else if (pull == 2) gpio_pull_down(pin);
        else {
            gpio_disable_pulls(pin);
        }
    }
}

void hal_gpio_write(int pin, bool value) {
    gpio_put(pin, value);
}

bool hal_gpio_read(int pin) {
    // RP2350-E9（Bank 0 のプルダウン ラッチアップ）への対策。
    //
    // 入力に設定して内蔵プルダウンを有効にしたパッドは、入力バッファを
    // 有効にしたままだとリーク電流（IOVDD=3.3V で最大 120uA）でおよそ
    // 2.1〜2.2V に張り付いてしまい、プルダウン（約 50-80kΩ）では 0 に
    // 引き戻せない。結果、開放ピンでも常に 1 と読めてしまう。
    //
    // データシートの回避策どおり、入力バッファを一度切ってパッドを放電させ、
    // 読む直前だけ有効にして即座に読む。読み終えたらまた切っておく
    // （リークが止まるので消費電流の面でも推奨されている状態）。
    //
    // 内蔵プルアップはこの問題の影響を受けないため、そのまま読む。
    // 出力ピンはパッドを能動的に駆動しているので同じく対象外。
    if (!gpio_is_dir_out(pin) && gpio_is_pulled_down(pin)) {
        gpio_set_input_enabled(pin, false);
        // プルダウンでパッドが 0 まで落ちるのを待つ。配線が付くと容量が
        // 増えるため、時定数に対して十分な余裕を取っている
        busy_wait_us(50);
        gpio_set_input_enabled(pin, true);
        bool value = gpio_get(pin);
        gpio_set_input_enabled(pin, false);
        return value;
    }
    return gpio_get(pin);
}
#else
// ホストでは tests/mock_hal_display.cpp が GPIO をスタブ実装
#endif

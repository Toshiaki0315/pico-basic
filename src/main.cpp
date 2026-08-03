#include "pico/stdlib.h"
#include <stdio.h>
#include "hal_display.h"
#include "hal_sdcard.h"
#include "hal_sound.h"
#include "hal_touch.h"
#include "hal_battery.h"
#include "hal_adc.h"
#include "hal_imu.h"
#include "hal_rtc.h"
#include "repl.h"

int main() {
    // 電池パスのラッチを最初に保持する。
    // 表示の初期化にはスプラッシュの 2 秒待ちがあるため、それより後にすると
    // Key2 で電池起動したときにボタンを 2 秒以上押し続ける必要が出てしまう
    hal_battery_power_latch_hold();

    // Initialize standard I/O (USB CDC)
    stdio_init_all();

    // Phase 2: Initialize Waveshare 2.8" Touch LCD via SPI
    hal_display_init();

    // Phase 3: MicroSD (SAVE / LOAD / FILES)
    hal_sdcard_init();

    // Phase 5: I2S sound output (PIO + DMA) and I2C touch panel
    hal_sound_init();
    hal_touch_init();
    hal_battery_init();
    // ADIN / CPUTEMP 用。adc_init() は ADC ブロックをリセットするので、
    // 温度センサーを有効にするこちらを必ず hal_battery_init() の後に呼ぶこと
    hal_adc_init();
    // IMU はタッチと同じ i2c1 を使うので、hal_touch_init() の後に呼ぶ
    hal_imu_init();
    hal_rtc_init();

    // 起動したことが音でも分かるように（非同期なので待たされない）
    hal_sound_startup_chime();

    // Phase 1 & 2: Start the BASIC Read-Eval-Print Loop
    // REPL handles both Serial and LCD outputs
    repl_start();

    return 0;
}

#include "pico/stdlib.h"
#include <stdio.h>
#include "hal_display.h"
#include "hal_sdcard.h"
#include "hal_sound.h"
#include "hal_touch.h"
#include "repl.h"

int main() {
    // Initialize standard I/O (USB CDC)
    stdio_init_all();

    // Phase 2: Initialize Waveshare 2.8" Touch LCD via SPI
    hal_display_init();

    // Phase 3: MicroSD (SAVE / LOAD / FILES)
    hal_sdcard_init();

    // Phase 5: I2S sound output (PIO + DMA) and I2C touch panel
    hal_sound_init();
    hal_touch_init();

    // 起動したことが音でも分かるように（非同期なので待たされない）
    hal_sound_startup_chime();

    // Phase 1 & 2: Start the BASIC Read-Eval-Print Loop
    // REPL handles both Serial and LCD outputs
    repl_start();

    return 0;
}

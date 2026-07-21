/* hw_config.c
 *
 * Waveshare RP2350-Touch-LCD-2.8 の MicroSD カードスロット定義。
 * no-OS-FatFS-SD-SDIO-SPI-RPi-Pico ライブラリがこのファイルの
 * sd_get_num() / sd_get_by_num() を呼び出してハードウェア構成を得る。
 *
 * ── SDIO を使う理由 ──────────────────────────────────────────
 * 配線は SD_CLK=GP19 / SD_CMD=GP20 / SD_D0=GP21 / D1=GP22 / D2=GP23 / D3=GP24
 * （公式回路図のネットリストで確認）。
 * RP2350 のハードウェア SPI は GPIO ごとに割り当てられる役割が固定されており、
 * GP19 は SPI0 TX、GP20 は SPI0 RX、GP21 は SPI0 CSn である。
 * SCK を GP19 に出すことはできないため、**この配線では SPI モードは成立しない**。
 * PIO なら任意のピンを駆動できるので、SDIO モード（PIO 実装）を使う。
 * Waveshare 公式デモも同じライブラリを SDIO モードで使っている。
 *
 * ── 他ペリフェラルとの棲み分け ───────────────────────────────
 * PIO : SDIO は pio1（ドライバが GPIO_FUNC_PIO1 を直接指定するため変更不可）。
 *       I2S サウンドは pio0 なので競合しない。
 * DMA : SDIO は DMA_IRQ_1、I2S サウンドは DMA_IRQ_0（排他ハンドラ）。
 *       どちらかを変える場合は両方を見直すこと。
 */

#include "hw_config.h"

/* CLK / D1 / D2 / D3 はここで設定してはいけない。
   ライブラリが D0 からのオフセットで自動計算する（設定済みだと assert する）。
     CLK = D0 - 2 = 19, D1 = 22, D2 = 23, D3 = 24 */
static sd_sdio_if_t sdio_if = {
    .CMD_gpio = 20,
    .D0_gpio  = 21,

    .SDIO_PIO    = pio1,
    .DMA_IRQ_num = DMA_IRQ_1,
    .use_exclusive_DMA_IRQ_handler = true,

    .baud_rate = 125 * 1000 * 1000 / 6, // 約 20.8 MHz（公式デモと同じ）
};

static sd_card_t sd_card = {
    .type      = SD_IF_SDIO,
    .sdio_if_p = &sdio_if,
};

size_t sd_get_num() { return 1; }

sd_card_t *sd_get_by_num(size_t num) {
    return (0 == num) ? &sd_card : NULL;
}

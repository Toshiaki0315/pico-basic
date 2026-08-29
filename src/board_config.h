#pragma once
#include <cstdint>

// ---------------------------------------------------------
// ボードの配線定義
//
// **別の RP2350 ボードへ移植するときは、原則このファイルだけを差し替える。**
//
// ここに置くのは「基板の配線」だけ。チップ固有のレジスタ定義（CST328 /
// QMI8658 / PCF85063A のレジスタ番号やビット位置）は各 hal_*.cpp に残してある。
// チップが変わればドライバごと差し替えることになり、配線表に混ぜても
// 使い回せないため。
//
// ピン番号は constexpr で置く（コーディング標準 §3）。マクロのままだと型が
// 付かず、誤って別の意味の数と混ぜても何も言われないため。
// SPI / I2C のインスタンスだけはマクロで残してある。spi1 / i2c1 は SDK が
// レジスタのアドレスをポインタへキャストしたもので、constexpr にできない。
//
// 現在の対象: Waveshare RP2350-Touch-LCD-2.8
// ピン割り当ては公式回路図のネットリストで確認済み。
// ---------------------------------------------------------

constexpr const char* BOARD_NAME = "Waveshare RP2350-Touch-LCD-2.8";

// ---- LCD（ST7789V / SPI）----
#define BOARD_LCD_SPI spi1 // ポインタキャストなのでマクロ（上の説明を参照）
constexpr int BOARD_LCD_MOSI   = 11;
constexpr int BOARD_LCD_MISO   = 12;
constexpr int BOARD_LCD_SCK    = 10;
constexpr int BOARD_LCD_CS     = 13;
constexpr int BOARD_LCD_DC     = 14;
constexpr int BOARD_LCD_RST    = 15;
constexpr int BOARD_LCD_BL     = 16;  // バックライト（PWM で調光）
constexpr int BOARD_LCD_WIDTH  = 320; // 横向きで使用
constexpr int BOARD_LCD_HEIGHT = 240;

// ---- I2C（タッチ・IMU・RTC が 1 本のバスを共用）----
// アドレスが違うので競合しない: タッチ 0x1A / RTC 0x51 / IMU 0x6B
#define BOARD_I2C i2c1 // ポインタキャストなのでマクロ（上の説明を参照）
constexpr int      BOARD_I2C_SDA  = 6;
constexpr int      BOARD_I2C_SCL  = 7;
constexpr uint32_t BOARD_I2C_BAUD = 400 * 1000;

constexpr int BOARD_TP_RST   = 17; // タッチ（CST328）のリセット
constexpr int BOARD_TP_INT   = 18; // 同 割り込み。本実装はポーリングなので未使用
constexpr int BOARD_IMU_INT1 = 8;  // IMU（QMI8658）の割り込み。未使用
constexpr int BOARD_IMU_INT2 = 9;
constexpr int BOARD_RTC_INT  = 5;  // RTC（PCF85063A）の割り込み。未使用

// ---- I2S サウンド（PCM5101A + APA2068）----
constexpr int BOARD_I2S_BCK  = 2;
constexpr int BOARD_I2S_LRCK = 3;
constexpr int BOARD_I2S_DIN  = 4;

// ---- バッテリ ----
// VBAT --[R1 200K]--+-- BAT_ADC --[R4 100K]-- GND
// ADC が見るのは VBAT の 1/3 なので、読み値を 3 倍して VBAT を得る
constexpr int   BOARD_BAT_ADC_GPIO  = 27;
constexpr int   BOARD_BAT_ADC_INPUT = 1; // RP2350: GPIO26=0 / 27=1 / 28=2 / 29=3
constexpr float BOARD_BAT_DIVIDER   = 3.0f;
// 電池パスのラッチ制御線。**High で電池パスが開く**（hal_battery.h に回路の説明）。
// 起動時に High にしないと、USB を抜いた瞬間に電源が落ちる。
// アナログ入力に切り替えると出力が切れて電源が落ちるので ADIN の対象外にしてある
constexpr int BOARD_BAT_EN_GPIO = 26;
// 電源ボタン Key2（KEY_BAT）。3V3 に R8 10K でプルアップ、押すと GND。
// 押すと Q1 のゲートを直接引き下げるので、電源を入れる側はハードだけで完結する
constexpr int BOARD_KEY_GPIO = 25;

// ---- 拡張コネクタに出ている未使用ピン（ADIN / PIN で使える）----
constexpr int BOARD_FREE_GPIO_A = 28; // ADC2
constexpr int BOARD_FREE_GPIO_B = 29; // ADC3

// ---- ADC 共通 ----
constexpr float BOARD_ADC_VREF = 3.3f;    // 3V3 レギュレータ
constexpr float BOARD_ADC_MAX  = 4095.0f; // 12bit

// ---- MicroSD ----
// 配線の都合で SPI が使えず SDIO 固定。理由と結線は src/hw_config.c を参照。
// ビルド時の切り替えに使う想定なのでこれはマクロのまま
#define BOARD_SD_USES_SDIO 1

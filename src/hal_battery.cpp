#include "hal_battery.h"

#if __has_include("pico/stdlib.h")
#include "hardware/adc.h"
#include "pico/stdlib.h"
#include "board_config.h"
#include "pico/stdio_usb.h"

// BAT_ADC は GPIO27。RP2350 の ADC 入力番号は GPIO26=0, 27=1, 28=2, 29=3
#define BAT_ADC_GPIO BOARD_BAT_ADC_GPIO
#define BAT_ADC_INPUT BOARD_BAT_ADC_INPUT

// 分圧は 200K:100K なので ADC 電圧の 3 倍が VBAT
#define BAT_DIVIDER BOARD_BAT_DIVIDER
// ADC の基準電圧（3V3 レギュレータ）と 12bit の分解能
#define ADC_VREF BOARD_ADC_VREF
#define ADC_MAX BOARD_ADC_MAX

static bool adc_ready = false;

// 電源ボタンの長押し判定。押しっぱなしがこの時間続いたら電源を切る
#define POWER_KEY_HOLD_MS 2000

static uint32_t key_down_since = 0;
static bool     key_fired      = false;

void hal_battery_power_key_init() {
  gpio_init(BOARD_KEY_GPIO);
  gpio_set_dir(BOARD_KEY_GPIO, GPIO_IN);
  gpio_pull_up(BOARD_KEY_GPIO); // 基板にも R8 10K があるが念のため
}

bool hal_battery_power_key_held() {
  bool down = !gpio_get(BOARD_KEY_GPIO); // プルアップなので押すと Low
  if (!down) {
    key_down_since = 0;
    key_fired = false;
    return false;
  }
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if (key_down_since == 0) {
    key_down_since = now;
    return false;
  }
  if (key_fired) return false; // 1 回の長押しにつき 1 度だけ
  if (now - key_down_since < POWER_KEY_HOLD_MS) return false;
  key_fired = true;
  return true;
}

void hal_battery_power_off() {
  gpio_put(BOARD_BAT_EN_GPIO, 0); // T1 が切れ、R9 が Q1 のゲートを引き上げて遮断
}

void hal_battery_power_latch_hold() {
  // BAT_EN=High で T1 が導通し、Q1 のゲートを引き下げて電池パスを開く。
  // USB 接続中でも Q2 が遮断しているので競合しない（詳細は hal_battery.h）
  gpio_init(BOARD_BAT_EN_GPIO);
  gpio_set_dir(BOARD_BAT_EN_GPIO, GPIO_OUT);
  gpio_put(BOARD_BAT_EN_GPIO, 1);
}

void hal_battery_init() {
  adc_init();
  adc_gpio_init(BAT_ADC_GPIO); // デジタル入力を切ってアナログ入力にする
  adc_ready = true;
}

int hal_battery_millivolts() {
  if (!adc_ready) hal_battery_init();

  // 1 サンプルだと LCD の SPI 転送などのノイズが乗るので平均を取る
  adc_select_input(BAT_ADC_INPUT);
  uint32_t sum = 0;
  const int samples = 16;
  for (int i = 0; i < samples; i++) sum += adc_read();

  float raw = (float)sum / (float)samples;
  float volts = raw * (ADC_VREF / ADC_MAX) * BAT_DIVIDER;
  return (int)(volts * 1000.0f + 0.5f);
}

int hal_battery_usb_connected() {
  // VBUS を読む配線が無いので、USB シリアルの接続状態で代用する
  return stdio_usb_connected() ? 1 : 0;
}

void hal_battery_set_mock_millivolts(int) {
  // 実機では何もしない（テスト専用の入口）
}
void hal_battery_set_mock_usb(int) {}

#else
// ---------------------------------------------------------
// ホストビルド用。テストから値を差し込めるようにしておく
// ---------------------------------------------------------
static int mock_mv = 0;
static int mock_usb = 0;

// ホストには電源ラッチもボタンも無い
void hal_battery_power_latch_hold() {}
void hal_battery_power_off() {}
void hal_battery_power_key_init() {}
bool hal_battery_power_key_held() { return false; }

void hal_battery_init() { mock_mv = 0; mock_usb = 0; }

int hal_battery_millivolts() { return mock_mv; }
int hal_battery_usb_connected() { return mock_usb; }

void hal_battery_set_mock_millivolts(int mv) { mock_mv = mv; }
void hal_battery_set_mock_usb(int connected) { mock_usb = connected ? 1 : 0; }

#endif

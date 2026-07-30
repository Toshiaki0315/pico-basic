#include "hal_battery.h"

#if __has_include("pico/stdlib.h")
#include "hardware/adc.h"
#include "pico/stdlib.h"

// BAT_ADC は GPIO27。RP2350 の ADC 入力番号は GPIO26=0, 27=1, 28=2, 29=3
#define BAT_ADC_GPIO 27
#define BAT_ADC_INPUT 1

// 分圧は 200K:100K なので ADC 電圧の 3 倍が VBAT
#define BAT_DIVIDER 3.0f
// ADC の基準電圧（3V3 レギュレータ）と 12bit の分解能
#define ADC_VREF 3.3f
#define ADC_MAX 4095.0f

static bool adc_ready = false;

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

void hal_battery_set_mock_millivolts(int) {
  // 実機では何もしない（テスト専用の入口）
}

#else
// ---------------------------------------------------------
// ホストビルド用。テストから値を差し込めるようにしておく
// ---------------------------------------------------------
static int mock_mv = 0;

void hal_battery_init() { mock_mv = 0; }

int hal_battery_millivolts() { return mock_mv; }

void hal_battery_set_mock_millivolts(int mv) { mock_mv = mv; }

#endif

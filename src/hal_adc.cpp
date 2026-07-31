#include "hal_adc.h"

#if __has_include("pico/stdlib.h")
#include "hardware/adc.h"
#include "pico/stdlib.h"

// ADC の基準電圧（3V3 レギュレータ）と 12bit の分解能
#define ADC_VREF 3.3f
#define ADC_MAX 4095.0f

// 1 サンプルだと LCD の SPI 転送などのノイズが乗るので平均を取る（hal_battery と同じ方針）
#define ADC_SAMPLES 16

static bool adc_ready = false;

// hal_battery も adc_init() を呼ぶが、ADC ブロックの初期化は冪等で、
// adc_gpio_init() が触るパッド設定は ADC ブロックのリセットでは戻らない。
// そのため両者がそれぞれ自前のフラグを持っていて問題ない。
void hal_adc_init() {
  adc_init();
  // デジタル入力バッファとプルを切ってアナログ入力にする
  adc_gpio_init(27);
  adc_gpio_init(28);
  adc_gpio_init(29);
  adc_set_temp_sensor_enabled(true);
  adc_ready = true;
}

static uint32_t read_averaged(int input) {
  adc_select_input(input);
  uint32_t sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) sum += adc_read();
  return sum / ADC_SAMPLES;
}

int hal_adc_read_raw(int gpio) {
  if (!adc_pin_allowed(gpio)) return -1;
  if (!adc_ready) hal_adc_init();
  return (int)read_averaged(gpio - 26);
}

float hal_adc_read_temp_c() {
  if (!adc_ready) hal_adc_init();
  float volts = (float)read_averaged(4) * (ADC_VREF / ADC_MAX);
  // RP2350 データシートの近似式。校正していないので数℃の個体差がある
  return 27.0f - (volts - 0.706f) / 0.001721f;
}

void hal_adc_set_mock_raw(int, int) {
  // 実機では何もしない（テスト専用の入口）
}
void hal_adc_set_mock_temp_c(float) {}

#else
// ---------------------------------------------------------
// ホストビルド用。テストから値を差し込めるようにしておく
// ---------------------------------------------------------
static int mock_raw[4];   // 添字 = gpio - 26
static float mock_temp;

void hal_adc_init() {
  for (int i = 0; i < 4; i++) mock_raw[i] = 0;
  mock_temp = 0.0f;
}

int hal_adc_read_raw(int gpio) {
  if (!adc_pin_allowed(gpio)) return -1;
  return mock_raw[gpio - 26];
}

float hal_adc_read_temp_c() { return mock_temp; }

void hal_adc_set_mock_raw(int gpio, int raw) {
  if (gpio >= 26 && gpio <= 29) mock_raw[gpio - 26] = raw;
}
void hal_adc_set_mock_temp_c(float celsius) { mock_temp = celsius; }

#endif

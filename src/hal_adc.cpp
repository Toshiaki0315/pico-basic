#include "hal_adc.h"

#if __has_include("pico/stdlib.h")
#include "hardware/adc.h"
#include "pico/stdlib.h"

// 1 サンプルだと LCD の SPI 転送などのノイズが乗るので平均を取る（hal_battery と同じ方針）
#define ADC_SAMPLES 16

// 温度はサンプルを増やしてオーバーサンプリングする。
// ADC の 1 LSB は約 0.47℃ もあり、そのままでは値が飛び飛びになるため。
// ノイズが LSB をまたいで揺れることを利用して、平均の小数部から分解能を稼ぐ。
#define ADC_TEMP_SAMPLES 256

static bool block_ready = false;
static bool adc_ready = false;

void hal_adc_block_init() {
  if (block_ready) return; // adc_init() はブロックをリセットするので一度だけ
  adc_init();
  adc_set_temp_sensor_enabled(true);
  block_ready = true;
}

void hal_adc_init() {
  hal_adc_block_init();
  // ADIN で読む対象だけをアナログ入力にする（デジタル入力バッファとプルを切る）。
  // BAT_EN を含めないのは adc_pin_allowed() が弾いてくれるから
  for (int gpio = ADC_GPIO_BASE; gpio < ADC_GPIO_BASE + ADC_INPUT_COUNT; gpio++) {
    if (adc_pin_allowed(gpio)) adc_gpio_init(gpio);
  }
  adc_ready = true;
}

// 平均は小数のまま返すこと。整数で割ってしまうと平均した意味が無くなり、
// 分解能が 1 サンプルのときと変わらなくなる（温度が 0.47℃ 刻みで飛ぶ）。
static float read_averaged(int input, int samples) {
  adc_select_input(input);
  uint32_t sum = 0;
  for (int i = 0; i < samples; i++) sum += adc_read();
  return (float)sum / (float)samples;
}

int hal_adc_read_raw(int gpio) {
  if (!adc_pin_allowed(gpio)) return -1;
  if (!adc_ready) hal_adc_init();
  // ADIN の戻り値は 0-4095 の整数と決めているので、ここで四捨五入する
  return (int)(read_averaged(adc_input_for_gpio(gpio), ADC_SAMPLES) + 0.5f);
}

float hal_adc_read_temp_c() {
  if (!adc_ready) hal_adc_init();
  float volts = read_averaged(ADC_TEMP_INPUT, ADC_TEMP_SAMPLES) *
                (BOARD_ADC_VREF / BOARD_ADC_MAX);
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
static int mock_raw[ADC_INPUT_COUNT];
static float mock_temp;

void hal_adc_block_init() {}

void hal_adc_init() {
  for (int i = 0; i < ADC_INPUT_COUNT; i++) mock_raw[i] = 0;
  mock_temp = 0.0f;
}

int hal_adc_read_raw(int gpio) {
  if (!adc_pin_allowed(gpio)) return -1;
  return mock_raw[adc_input_for_gpio(gpio)];
}

float hal_adc_read_temp_c() { return mock_temp; }

void hal_adc_set_mock_raw(int gpio, int raw) {
  int input = adc_input_for_gpio(gpio);
  if (input >= 0) mock_raw[input] = raw;
}
void hal_adc_set_mock_temp_c(float celsius) { mock_temp = celsius; }

#endif

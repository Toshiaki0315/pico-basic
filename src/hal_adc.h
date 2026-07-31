#pragma once

// 汎用アナログ入力と内蔵温度センサー（RP2350）
//
// RP2350 の ADC 入力番号は GPIO26=0 / 27=1 / 28=2 / 29=3 で、入力 4 が内蔵温度センサー。
// 本基板（Waveshare RP2350-Touch-LCD-2.8）での割り当ては回路図より:
//   GPIO26 = BAT_EN  … 電源ラッチの制御線。アナログ入力に切り替えると
//                        デジタル出力が切れて電源が落ちうるので、意図的に対象外にする
//   GPIO27 = BAT_ADC … 電池電圧の分圧（hal_battery が使用）。読むだけなら無害
//   GPIO28 / GPIO29  … 拡張コネクタに出ている未使用ピン。ここが本来の用途

void hal_adc_init();

// 指定 GPIO の変換値 0-4095。読めないピンは -1。
int hal_adc_read_raw(int gpio);

// 内蔵温度センサーの摂氏温度。
float hal_adc_read_temp_c();

// ホストテスト用: 実機ビルドでは何もしない。
void hal_adc_set_mock_raw(int gpio, int raw);
void hal_adc_set_mock_temp_c(float celsius);

// ---------------------------------------------------------
// ハードウェアに依存しない判定（ADIN 関数と共有する）
// ---------------------------------------------------------

// ADIN で読んでよい GPIO か。26 は BAT_EN なので除外している（上のコメント参照）。
inline bool adc_pin_allowed(int gpio) {
    return gpio >= 27 && gpio <= 29;
}

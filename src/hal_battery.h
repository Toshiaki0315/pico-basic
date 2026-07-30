#pragma once

// バッテリ電圧の読み取り（Waveshare RP2350-Touch-LCD-2.8）
//
// 回路図（RP2350-Touch-LCD-2.8-Schematic.pdf）より:
//   VBAT --[R1 200K]--+-- BAT_ADC (GPIO27 = ADC1) --[R4 100K]-- GND
//                     +-- C13/C14 100nF
// つまり ADC が見る電圧は VBAT の 1/3。VBAT は J1 (PH1.25 2P) のリチウムポリマー電池に直結。
//
// 注意: 同じブロックにある BAT_EN (GPIO26) は電源ラッチの制御線で、
// 誤って駆動すると電源が落ちる可能性がある。ここでは触れないこと。

void hal_battery_init();

// 電池電圧をミリボルトで返す。読めない場合は 0。
int hal_battery_millivolts();

// ホストテスト用: 次に hal_battery_millivolts() が返す値を差し替える。
// 実機ビルドでは何もしない。
void hal_battery_set_mock_millivolts(int mv);

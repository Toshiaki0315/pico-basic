#pragma once

// バッテリ電圧の読み取り（Waveshare RP2350-Touch-LCD-2.8）
//
// 回路図（RP2350-Touch-LCD-2.8-Schematic.pdf）より:
//   VBAT --[R1 200K]--+-- BAT_ADC (GPIO27 = ADC1) --[R4 100K]-- GND
//                     +-- C13/C14 100nF
// つまり ADC が見る電圧は VBAT の 1/3。VBAT は J1 (PH1.25 2P) のリチウムポリマー電池に直結。
//
// BAT_EN (GPIO26) は電池パスのラッチ制御線。回路図より:
//   BAT_EN --[R15 1K]-- T1(NPN 8050) のベース（R16 10K でベース→GND）
//   T1 のコレクタ -> Q1(AO3401 P-ch) のゲート（R9 100K で VBAT にプルアップ）
//   Q1: VBAT -> VSYS
// P-ch なのでゲートを Low に引くと導通する。つまり **BAT_EN=High で電池パスが開く**。
// ベースがプルダウンされているので、駆動しなければ電池パスは切れたまま。
//
// もう一段の Q2 は VBUS でゲートが引き上げられ、USB があるうちは電池パスを遮断する。
// そのため USB 接続中に BAT_EN を立てても電池と USB がぶつかることはない。
// **Low にすると電池運用時にその場で電源が落ちる**ので、下げてはならない。

// 電池パスのラッチを保持する（BAT_EN=High）。
// **main() の先頭で、他の初期化より前に呼ぶこと。** Key2 で電池起動したとき、
// ここを通るまでボタンを押し続けないと電源が落ちるため。
void hal_battery_power_latch_hold();

// 電池パスを閉じて電源を切る（BAT_EN=Low）。
// USB 給電中は USB から電源が来続けるので切れない。
// また Key2 を押している間はボタンが直接ゲートを引くので、離すまで切れない。
void hal_battery_power_off();

// 電源ボタン(Key2)が規定時間押し続けられたら true を返す（1 回の長押しにつき 1 度）。
// 電源を切る処理は呼び出し側で行う（開いているファイルを閉じたいため）。
void hal_battery_power_key_init();
bool hal_battery_power_key_held();

void hal_battery_init();

// 電池電圧をミリボルトで返す。読めない場合は 0。
int hal_battery_millivolts();

// USB から給電されているか（1=USB、0=電池のみ）。
//
// 基板には VBUS を MCU に戻す配線が無いため、USB の CDC が
// ホストと接続されているか（pico の stdio_usb_connected）で代用している。
// そのため「データ通信をしない充電器だけに挿した場合」は 0 を返す。
int hal_battery_usb_connected();

// ホストテスト用: 次に hal_battery_millivolts() が返す値を差し替える。
// 実機ビルドでは何もしない。
void hal_battery_set_mock_millivolts(int mv);
void hal_battery_set_mock_usb(int connected);

// ---------------------------------------------------------
// 電圧の解釈（ハードウェアに依存しない純粋な計算）。
// BATTERY() 関数と BATTERY 文の両方から使うのでここに置く。
// ---------------------------------------------------------

// 残量の目安 0-100%。3.30V を 0%、4.20V を 100% とした直線近似。
// リチウムポリマーの放電曲線は中間が平坦なので、あくまで目安。
inline int battery_percent_from_mv(int mv) {
    int pct = (mv - 3300) * 100 / (4200 - 3300);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

// 電池の有無。1=有り / 2=判別不能。
//
// USB 非接続で動いているなら電源は電池しかないので確実に「有り」。
// USB 給電中は充電 IC が満充電電圧(約4.2V)まで持ち上げるため、電池が無くても
// 満充電と同じ電圧に見える。逆に電圧がそこまで届いていなければ、電池が負荷に
// なって引き下げている＝電池がある証拠になる。
//
// **「無し(0)」は返さない。** この基板には電池の有無を直接知る配線が無く、
// 電池が「無い」ことは証明できないため。
//
// 実測（両方ともこの基板で確認）:
//   電池なし + USB      → 4196mV（充電 IC が浮動電圧まで持ち上げる）
//   空の電池 + USB      → 2340mV（電池が引き下げる。充電されて徐々に上がる）
// 以前は 2500mV 未満を「電池なし」と判定していたが、実測はその逆で、
// 低電圧はむしろ「深く放電した電池がある」ことを示していた。
inline int battery_presence(int mv, int usb_connected) {
    if (!usb_connected) return 1; // 電源が電池しかない
    if (mv < 4100) return 1;      // 浮動電圧に届いていない = 電池が負荷になっている
    return 2;                     // 満充電の電池と区別が付かない
}

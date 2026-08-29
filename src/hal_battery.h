#pragma once

/// @file hal_battery.h
/// 電池電圧・電源ラッチ・電源ボタン。
#include <cstdint>

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

/**
 * @brief 電池パスのラッチを保持する（BAT_EN=High）。
 *
 * **main() の先頭で、他の初期化より前に呼ぶこと。** Key2 で電池起動したとき、
 * ここを通るまでボタンを押し続けないと電源が落ちるため。
 */
void hal_battery_power_latch_hold();

/**
 * @brief 電池パスを閉じて電源を切る（BAT_EN=Low）。
 *
 * USB 給電中は USB から電源が来続けるので切れない。
 * また Key2 を押している間はボタンが直接ゲートを引くので、離すまで切れない。
 */
void hal_battery_power_off();

/// @brief 電源ボタン(Key2)のピンをプルアップ付き入力にする
void hal_battery_power_key_init();

/**
 * @brief 電源ボタン(Key2)が規定時間押し続けられたかを調べる。
 * @return 長押しが成立した「瞬間」だけ true（1 回の長押しにつき 1 度）。
 *         電源を切る処理は呼び出し側で行う（開いているファイルを閉じたいため）
 */
bool hal_battery_power_key_held();

// ---------------------------------------------------------
// 長押し判定のロジック（ハードウェアに依存しない）
//
// GPIO を読む部分・時刻を取る部分と切り離してあるのは、下の
// battery_percent_from_mv() などと同じ理由。しきい値ぎりぎりの挙動や
// 「1 回の長押しにつき 1 度だけ」は実機に触らないと確かめられない場所に
// あると誰も確かめないので、ホストのテストから叩ける形にしておく。
// ---------------------------------------------------------

// 押しっぱなしがこの時間続いたら長押しと見なす
constexpr uint32_t POWER_KEY_HOLD_MS = 2000;

struct PowerKeyState {
    bool     down_seen  = false; // 押されている状態を見たか
    uint32_t down_since = 0;     // 押し始めの時刻
    bool     fired      = false; // この長押しでは既に知らせた
};

// ポーリング 1 回ぶん。長押しが成立した「瞬間」だけ true を返す。
//
// 押し始めを down_since ではなく down_seen で覚えるのは、起動直後は
// now_ms が 0 になり得るため。0 を「押していない」の目印に使うと、
// その 1ms の間だけ時間の計測が始まらない。
/**
 * @brief 長押し判定のポーリング 1 回ぶん。
 * @param st 押し始めからの状態。呼び出し側が保持する
 * @param down いまボタンが押されているか
 * @param now_ms 現在時刻（ミリ秒）
 * @return 長押しが成立した「瞬間」だけ true
 */
inline bool power_key_step(PowerKeyState& st, bool down, uint32_t now_ms) {
    if (!down) { // 離した。次の長押しに備えて畳む
        st.down_seen = false;
        st.fired     = false;
        return false;
    }
    if (!st.down_seen) { // 押し始め。ここから時間を測る
        st.down_seen  = true;
        st.down_since = now_ms;
        return false;
    }
    if (st.fired) return false; // 1 回の長押しにつき 1 度だけ
    if (now_ms - st.down_since < POWER_KEY_HOLD_MS) return false;
    st.fired = true;
    return true;
}

/// @brief 電池電圧を読む ADC ピンを用意する
void hal_battery_init();

// 電池電圧をミリボルトで返す。読めない場合は 0。
/**
 * @brief 電池電圧を読む（16 サンプルの平均）。
 * @return ミリボルト。読めない場合は 0
 */
int hal_battery_millivolts();

// USB から給電されているか（1=USB、0=電池のみ）。
//
// 基板には VBUS を MCU に戻す配線が無いため、USB の CDC が
// ホストと接続されているか（pico の stdio_usb_connected）で代用している。
// そのため「データ通信をしない充電器だけに挿した場合」は 0 を返す。
/**
 * @brief USB から給電されているか。
 * @return 給電中なら 1。VBUS の配線が無いので USB シリアルの接続状態で代用する
 */
int hal_battery_usb_connected();

// ホストテスト用: 次に hal_battery_millivolts() が返す値を差し替える。
// 実機ビルドでは何もしない。
/// @brief テスト用の差し込み口（実機では何もしない）
/**
 * @brief テスト用の差し込み口（実機では何もしない）。
 * @param mv 以降 hal_battery_millivolts() が返す値
 */
void hal_battery_set_mock_millivolts(int mv);
/**
 * @brief テスト用の差し込み口（実機では何もしない）。
 * @param connected 以降 hal_battery_usb_connected() が返す値
 */
void hal_battery_set_mock_usb(int connected);

// ---------------------------------------------------------
// 電圧の解釈（ハードウェアに依存しない純粋な計算）。
// BATTERY() 関数と BATTERY 文の両方から使うのでここに置く。
// ---------------------------------------------------------

// 残量の目安 0-100%。3.30V を 0%、4.20V を 100% とした直線近似。
// リチウムポリマーの放電曲線は中間が平坦なので、あくまで目安。
/**
 * @brief 電圧から残量の目安を出す。
 * @param mv 電池電圧（ミリボルト）
 * @return 0-100（%）
 */
inline int battery_percent_from_mv(int mv) {
    int pct = (mv - 3300) * 100 / (4200 - 3300);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

// 充電 IC が電池を持ち上げる浮動電圧。これに届いていれば満充電か電池なし。
// 実測 4196mV（電池なし + USB）なので、少し下に敷居を置く
constexpr int BATTERY_FLOAT_MV = 4100;

// 電池の有無。
//
// **「無し」は返さない。** この基板には電池の有無を直接知る配線が無く、
// 電池が「無い」ことは証明できないため。判別が付かないことを Unknown で表す。
//
// 値は BASIC の BATTERY(2) がそのまま返す数なので、MANUAL.md の記述と対で
// 決まっている。並べ替えたり詰め直したりしないこと。
enum class BatteryPresence {
    Present = 1, // 電池がある
    Unknown = 2, // 満充電の電池と、電池なしの浮動電圧が区別できない
};

// USB 非接続で動いているなら電源は電池しかないので確実に「有り」。
// USB 給電中は充電 IC が満充電電圧(約4.2V)まで持ち上げるため、電池が無くても
// 満充電と同じ電圧に見える。逆に電圧がそこまで届いていなければ、電池が負荷に
// なって引き下げている＝電池がある証拠になる。
//
// 実測（両方ともこの基板で確認）:
//   電池なし + USB      → 4196mV（充電 IC が浮動電圧まで持ち上げる）
//   空の電池 + USB      → 2340mV（電池が引き下げる。充電されて徐々に上がる）
// 以前は 2500mV 未満を「電池なし」と判定していたが、実測はその逆で、
// 低電圧はむしろ「深く放電した電池がある」ことを示していた。
/**
 * @brief 電池があるか判定する。
 * @param mv 電池電圧（ミリボルト）
 * @param usb_connected USB 給電中なら非 0
 * @return Present か Unknown。「無し」は返らない
 */
inline BatteryPresence battery_presence(int mv, int usb_connected) {
    if (!usb_connected) return BatteryPresence::Present;   // 電源が電池しかない
    if (mv < BATTERY_FLOAT_MV) return BatteryPresence::Present; // 電池が引き下げている
    return BatteryPresence::Unknown;                       // 満充電と区別が付かない
}

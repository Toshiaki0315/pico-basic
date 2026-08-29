#pragma once

/// @file hal_sound.h
/// I2S サウンド出力。発音は DMA で非同期に進む。
#include <stdint.h>

// ---------------------------------------------------------
// Sound Hardware Abstraction Layer
// ---------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

// PSG 相当の同時発音数（AY-3-8910 の 3ch に合わせる）
#define HAL_SOUND_VOICES 3

// 音量を指定しなかったときの既定値（0-15）。
// 矩形波は同じ振幅でも耳につきやすいため、最大にはしない。
// MML の V コマンドや hal_sound_set_volume() で上げ下げできる。
#define HAL_SOUND_DEFAULT_VOLUME 8

// 1 声分の発音指定。frequency <= 0 は消音（休符）
typedef struct {
    float frequency;
    int   volume; // 0-15
} HalSoundVoice;

// ---------------------------------------------------------
// 発音は非同期。
//
// 各関数は「発音キュー」に積むだけで、鳴り終わるのを待たずに戻る。
// 実際の再生は DMA 割り込み内のシーケンサが進めるため、演奏中も
// BASIC の実行を続けられる（PLAY したあとに描画する等）。
//
// キューが満杯のときだけ、空きができるまで待つ。
// ---------------------------------------------------------

/// @brief PIO と DMA を用意して I2S 出力を始める
void hal_sound_init();
/// @brief 短いビープを鳴らす
void hal_sound_beep();

// 単音発音（内部的には voice 0 のみを使う）
/**
 * @brief 単音を鳴らす（非同期。すぐ戻る）。
 * @param frequency 周波数（Hz）。0 以下なら休符
 * @param duration_ms 長さ（ミリ秒）
 */
void hal_sound_play(float frequency, int duration_ms);

// 最大 HAL_SOUND_VOICES 声を duration_ms だけ同時に発音する。
// 連続して積めば、周波数が変わらない声は位相を保って繋がる。
/**
 * @brief 複数声部を同時に鳴らす（非同期）。
 * @param voices 声部ごとの周波数と音量
 * @param duration_ms 長さ（ミリ秒）
 */
void hal_sound_play_voices(const HalSoundVoice voices[HAL_SOUND_VOICES], int duration_ms);

// キューを捨てて即座に全声を消音する（Ctrl-C や NEW から呼ぶ）
/// @brief 鳴っている音を止める
void hal_sound_stop();

// 再生中（キューに残りがある、または発音中）なら 1
/**
 * @brief まだ鳴っているか。
 * @return 鳴っていれば 1
 */
int hal_sound_is_playing();

// 起動音（PC-9801 風の 2 音「ピポッ」）を鳴らす
/// @brief 起動音を鳴らす（非同期なので待たされない）
void hal_sound_startup_chime();

/**
 * @brief 音量を変える。
 * @param volume 0-15
 */
void hal_sound_set_volume(int volume); // 0-15

// ---------------------------------------------------------
// PSG（AY-3-8910 相当）レジスタ。SOUND 命令用。
//
// これを呼ぶと PSG モードになり、レジスタ状態で鳴り続ける
// （PLAY / MUSIC のキュー再生とは排他。PLAY 等を呼ぶと PSG は止まる）。
//
//   R0/R1  ch A トーン周期（12bit: R0 下位8bit, R1 上位4bit）
//   R2/R3  ch B トーン周期
//   R4/R5  ch C トーン周期
//   R6     ノイズ周期（5bit, 0-31）
//   R7     ミキサー。bit0-2=トーン, bit3-5=ノイズ の有効(0)/無効(1)
//   R8/R9/R10  ch A/B/C 音量（bit0-3: 0-15、bit4: エンベロープ使用）
//   R11/R12    エンベロープ周期（16bit）
//   R13        エンベロープ形状
//
// トーン周波数 = 2MHz / (16 × 周期)
// ---------------------------------------------------------
#define HAL_SOUND_PSG_REGS 16
/**
 * @brief PSG 相当のレジスタへ書く（BASIC の SOUND 文）。
 * @param reg レジスタ番号（0-15）
 * @param data 書き込む値
 */
void hal_sound_psg_write(int reg, int data);

#ifdef __cplusplus
}
#endif

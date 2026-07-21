#pragma once
#include <stdint.h>

// ---------------------------------------------------------
// Sound Hardware Abstraction Layer
// ---------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

// PSG 相当の同時発音数（AY-3-8910 の 3ch に合わせる）
#define HAL_SOUND_VOICES 3

// 1 声分の発音指定。frequency <= 0 は消音（休符）
typedef struct {
    float frequency;
    int   volume; // 0-15
} HalSoundVoice;

void hal_sound_init();
void hal_sound_beep();

// 単音発音（内部的には voice 0 のみを使う）
void hal_sound_play(float frequency, int duration_ms);

// 最大 HAL_SOUND_VOICES 声を同時に duration_ms だけ発音する。
// 発音は終了後も継続するため、連続呼び出しで音を途切れさせずに繋げられる。
// 鳴り止めるには hal_sound_stop() を呼ぶこと。
void hal_sound_play_voices(const HalSoundVoice voices[HAL_SOUND_VOICES], int duration_ms);

// 全声を消音する
void hal_sound_stop();

void hal_sound_set_volume(int volume); // 0-15

#ifdef __cplusplus
}
#endif

#include "hal_sound.h"
#include <stdio.h>

#if __has_include("pico/stdlib.h")
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "i2s_out.pio.h"

// PIN configurations based on test repository
#define PIN_I2S_BCK     2
#define PIN_I2S_LRCK    3
#define PIN_I2S_DIN     4
// GP17 をミュート解除(XSMT)として扱っていたが、公式回路図では GP17 は
// タッチパネルのリセット線 (TP_RST)。このボードの DAC にミュート端子は
// 出ていないため、GP17 は hal_touch が使う。

#define SAMPLE_RATE         48000
#define AUDIO_BUFFER_SAMPLES    256

static uint32_t audio_buffer[2][AUDIO_BUFFER_SAMPLES];
static volatile int next_buffer_index = 0;
static int dma_chan;
static PIO audio_pio = pio0;
static uint audio_sm = 0;

// 位相は 0.0〜1.0 の正規化値。DMA 割り込み内でのみ更新する
static float voice_phase[HAL_SOUND_VOICES] = {0.0f, 0.0f, 0.0f};

// メイン側で書き換え、DMA 割り込みから読むため volatile
static volatile float voice_freq[HAL_SOUND_VOICES]  = {0.0f, 0.0f, 0.0f};
static volatile int   voice_volume[HAL_SOUND_VOICES] =
    {HAL_SOUND_DEFAULT_VOLUME, HAL_SOUND_DEFAULT_VOLUME, HAL_SOUND_DEFAULT_VOLUME};
static volatile int   current_volume = HAL_SOUND_DEFAULT_VOLUME; // hal_sound_play / BEEP 用のマスタ音量

// 3声を加算するため、1声あたりの振幅は全体の 1/3 に抑えてクリップを防ぐ
#define VOICE_MAX_AMPLITUDE (32767.0f / (float)HAL_SOUND_VOICES)

static void fill_audio_buffer(uint32_t* buffer, int samples) {
    // バッファ生成中に値が変わらないよう、先頭で一度だけ読み出す
    float freq[HAL_SOUND_VOICES];
    float amp[HAL_SOUND_VOICES];
    float phase_inc[HAL_SOUND_VOICES];
    bool  any_active = false;

    for (int v = 0; v < HAL_SOUND_VOICES; v++) {
        freq[v] = voice_freq[v];
        int vol = voice_volume[v];
        if (vol < 0) vol = 0;
        if (vol > 15) vol = 15;
        amp[v] = VOICE_MAX_AMPLITUDE * ((float)vol / 15.0f);
        phase_inc[v] = freq[v] / (float)SAMPLE_RATE;
        if (freq[v] > 0.0f) {
            any_active = true;
        } else {
            // 消音中の声は位相をリセットし、次の発音を必ず波形の先頭から始める
            voice_phase[v] = 0.0f;
        }
    }

    if (!any_active) {
        for (int i = 0; i < samples; i++) {
            buffer[i] = 0;
        }
        return;
    }

    for (int i = 0; i < samples; i++) {
        float mixed = 0.0f;

        for (int v = 0; v < HAL_SOUND_VOICES; v++) {
            if (freq[v] <= 0.0f) continue;

            // 矩形波（PSG 音色）: 前半周期で +amp、後半周期で -amp
            mixed += (voice_phase[v] < 0.5f) ? amp[v] : -amp[v];

            voice_phase[v] += phase_inc[v];
            if (voice_phase[v] >= 1.0f) {
                voice_phase[v] -= 1.0f;
            }
        }

        if (mixed > 32767.0f) mixed = 32767.0f;
        if (mixed < -32768.0f) mixed = -32768.0f;

        int16_t sample = (int16_t)mixed;
        buffer[i] = ((uint32_t)(uint16_t)sample << 16) | (uint32_t)(uint16_t)sample;
    }
}

static void __isr dma_handler() {
    dma_hw->ints0 = 1u << dma_chan;
    dma_channel_set_read_addr(dma_chan, audio_buffer[next_buffer_index], true);
    
    int buffer_to_fill = 1 - next_buffer_index;
    fill_audio_buffer(audio_buffer[buffer_to_fill], AUDIO_BUFFER_SAMPLES);
    next_buffer_index = buffer_to_fill;
}

void hal_sound_init() {
    uint pio_offset = pio_add_program(audio_pio, &i2s_out_program);
    audio_sm = pio_claim_unused_sm(audio_pio, true);
    i2s_out_program_init(audio_pio, audio_sm, pio_offset, PIN_I2S_BCK, PIN_I2S_DIN, SAMPLE_RATE);
    
    for (int v = 0; v < HAL_SOUND_VOICES; v++) {
        voice_phase[v] = 0.0f;
        voice_freq[v]  = 0.0f;
    }
    fill_audio_buffer(audio_buffer[0], AUDIO_BUFFER_SAMPLES);
    fill_audio_buffer(audio_buffer[1], AUDIO_BUFFER_SAMPLES);
    next_buffer_index = 1;
    
    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(audio_pio, audio_sm, true));
    
    dma_channel_configure(
        dma_chan,
        &c,
        &audio_pio->txf[audio_sm],
        audio_buffer[0],
        AUDIO_BUFFER_SAMPLES,
        false
    );
    
    dma_channel_set_irq0_enabled(dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    
    pio_sm_set_enabled(audio_pio, audio_sm, true);
    dma_channel_set_read_addr(dma_chan, audio_buffer[0], true);
}

void hal_sound_beep() {
    hal_sound_play(880.0f, 200);
}

void hal_sound_play(float frequency, int duration_ms) {
    HalSoundVoice voices[HAL_SOUND_VOICES] = {};
    voices[0].frequency = (frequency > 0) ? frequency : 0.0f;
    voices[0].volume    = current_volume;
    hal_sound_play_voices(voices, duration_ms);
    hal_sound_stop();
}

void hal_sound_play_voices(const HalSoundVoice voices[HAL_SOUND_VOICES], int duration_ms) {
    // 周波数が変わらない声は位相が維持されるため、続けて呼んでも音は途切れない
    for (int v = 0; v < HAL_SOUND_VOICES; v++) {
        voice_volume[v] = voices[v].volume;
        voice_freq[v]   = (voices[v].frequency > 0) ? voices[v].frequency : 0.0f;
    }

    sleep_ms(duration_ms);
}

void hal_sound_stop() {
    for (int v = 0; v < HAL_SOUND_VOICES; v++) {
        voice_freq[v] = 0.0f;
    }
}

void hal_sound_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 15) volume = 15;
    current_volume = volume;
}

#else
// Host Mock implementation
static int current_volume = HAL_SOUND_DEFAULT_VOLUME;

void hal_sound_init() {
    // Initialization for mock sound
}

void hal_sound_beep() {
    printf("[Sound] BEEP!\n");
}

void hal_sound_play(float frequency, int duration_ms) {
    if (frequency <= 0) {
        printf("[Sound] REST %d ms\n", duration_ms);
    } else {
        printf("[Sound] PLAY %0.2f Hz for %d ms (Vol: %d)\n", frequency, duration_ms, current_volume);
    }
}

void hal_sound_play_voices(const HalSoundVoice voices[HAL_SOUND_VOICES], int duration_ms) {
    int active = 0;
    for (int v = 0; v < HAL_SOUND_VOICES; v++) {
        if (voices[v].frequency > 0) active++;
    }

    if (active == 0) {
        printf("[Sound] REST %d ms\n", duration_ms);
        return;
    }

    printf("[Sound] PLAY %d voice(s) for %d ms:", active, duration_ms);
    for (int v = 0; v < HAL_SOUND_VOICES; v++) {
        if (voices[v].frequency > 0) {
            printf(" ch%d=%0.2fHz(Vol:%d)", v, voices[v].frequency, voices[v].volume);
        }
    }
    printf("\n");
}

void hal_sound_stop() {
    printf("[Sound] STOP\n");
}

void hal_sound_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 15) volume = 15;
    current_volume = volume;
    printf("[Sound] Volume set to %d\n", current_volume);
}

#endif

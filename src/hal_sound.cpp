#include "hal_sound.h"
#include <stdio.h>

#if __has_include("pico/stdlib.h")
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "i2s_out.pio.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// PIN configurations based on test repository
#define PIN_I2S_BCK     2
#define PIN_I2S_LRCK    3
#define PIN_I2S_DIN     4
#define PIN_PCM5101_XSMT 17

#define SAMPLE_RATE         48000
#define AUDIO_BUFFER_SAMPLES    256

static uint32_t audio_buffer[2][AUDIO_BUFFER_SAMPLES];
static volatile int next_buffer_index = 0;
static int dma_chan;
static PIO audio_pio = pio0;
static uint audio_sm = 0;

// sine_phase は DMA 割り込み内でのみ更新する
static float sine_phase = 0.0f;
// current_volume / current_freq はメイン側で書き換え、DMA 割り込みから読むため volatile
static volatile int current_volume = 15; // 0-15
static volatile float current_freq = 0.0f;

static void fill_audio_buffer(uint32_t* buffer, int samples) {
    // バッファ生成中に値が変わらないよう、先頭で一度だけ読み出す
    float freq = current_freq;
    int volume = current_volume;

    if (freq <= 0.0f) {
        for (int i = 0; i < samples; i++) {
            buffer[i] = 0;
        }
        sine_phase = 0.0f;
        return;
    }

    float phase_inc = 2.0f * (float)M_PI * freq / (float)SAMPLE_RATE;
    float vol = (float)volume / 15.0f;
    // Prevent clipping by applying a smaller base amplitude
    float amplitude = 32767.0f * vol * 0.5f; 
    
    for (int i = 0; i < samples; i++) {
        float sample_f = sinf(sine_phase) * amplitude;
        int16_t sample = (int16_t)sample_f;
        buffer[i] = ((uint32_t)(uint16_t)sample << 16) | (uint32_t)(uint16_t)sample;
        
        sine_phase += phase_inc;
        if (sine_phase >= 2.0f * (float)M_PI) {
            sine_phase -= 2.0f * (float)M_PI;
        }
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
    gpio_init(PIN_PCM5101_XSMT);
    gpio_set_dir(PIN_PCM5101_XSMT, GPIO_OUT);
    gpio_put(PIN_PCM5101_XSMT, 1);
    
    uint pio_offset = pio_add_program(audio_pio, &i2s_out_program);
    audio_sm = pio_claim_unused_sm(audio_pio, true);
    i2s_out_program_init(audio_pio, audio_sm, pio_offset, PIN_I2S_BCK, PIN_I2S_DIN, SAMPLE_RATE);
    
    sine_phase = 0.0f;
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
    if (frequency <= 0) {
        current_freq = 0;
    } else {
        current_freq = frequency;
    }
    sleep_ms(duration_ms);
    current_freq = 0;
}

void hal_sound_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 15) volume = 15;
    current_volume = volume;
}

#else
// Host Mock implementation
static int current_volume = 15;

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

void hal_sound_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 15) volume = 15;
    current_volume = volume;
    printf("[Sound] Volume set to %d\n", current_volume);
}

#endif

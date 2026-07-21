#include "hal_sound.h"
#include <stdio.h>

#if __has_include("pico/stdlib.h")
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
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

// ---------------------------------------------------------
// 発音キュー（単一生産者=メイン / 単一消費者=DMA 割り込み）
//
// メインは積むだけで待たないので、演奏中も BASIC を実行できる。
// 添字は自由に増やし続け、参照時にマスクする（満杯と空を区別するため）。
// ---------------------------------------------------------
#define SOUND_QUEUE_SIZE 128 // 2 のべき乗であること

typedef struct {
    float    freq[HAL_SOUND_VOICES];
    int      volume[HAL_SOUND_VOICES];
    uint32_t samples; // このステップを鳴らすサンプル数
} SoundStep;

static SoundStep sound_queue[SOUND_QUEUE_SIZE];
static volatile uint32_t queue_head = 0; // 割り込みが取り出す位置
static volatile uint32_t queue_tail = 0; // メインが積む位置

// 割り込み側だけが触る再生状態
static float    cur_freq[HAL_SOUND_VOICES]  = {0.0f, 0.0f, 0.0f};
static float    cur_amp[HAL_SOUND_VOICES]   = {0.0f, 0.0f, 0.0f};
static float    voice_phase[HAL_SOUND_VOICES] = {0.0f, 0.0f, 0.0f}; // 0.0〜1.0
static volatile uint32_t step_samples_left = 0;

// 停止要求。queue_head を触るのは割り込みだけにしたいので、
// メインはフラグを立てるだけにする（両方が head を書くと消し損ねる）
static volatile bool stop_requested = false;

static volatile int current_volume = HAL_SOUND_DEFAULT_VOLUME; // BEEP / SOUND 用

// 3声を加算するため、1声あたりの振幅は全体の 1/3 に抑えてクリップを防ぐ
#define VOICE_MAX_AMPLITUDE (32767.0f / (float)HAL_SOUND_VOICES)
#define SAMPLES_PER_MS (SAMPLE_RATE / 1000)

// 次のステップを取り出す。キューが空なら false
static bool sequencer_next_step() {
    if (queue_head == queue_tail) return false;

    const SoundStep* step = &sound_queue[queue_head & (SOUND_QUEUE_SIZE - 1)];
    for (int v = 0; v < HAL_SOUND_VOICES; v++) {
        cur_freq[v] = step->freq[v];

        int vol = step->volume[v];
        if (vol < 0) vol = 0;
        if (vol > 15) vol = 15;
        cur_amp[v] = VOICE_MAX_AMPLITUDE * ((float)vol / 15.0f);

        // 消音中の声は位相をリセットし、次の発音を波形の先頭から始める
        if (cur_freq[v] <= 0.0f) voice_phase[v] = 0.0f;
    }
    step_samples_left = step->samples;

    __dmb(); // 読み終えてから head を進める
    queue_head = queue_head + 1;
    return true;
}

static void fill_audio_buffer(uint32_t* buffer, int samples) {
    int i = 0;

    if (stop_requested) {
        queue_head = queue_tail;
        step_samples_left = 0;
        for (int v = 0; v < HAL_SOUND_VOICES; v++) {
            cur_freq[v]    = 0.0f;
            voice_phase[v] = 0.0f;
        }
        stop_requested = false;
    }

    while (i < samples) {
        if (step_samples_left == 0 && !sequencer_next_step()) {
            // キューが空。残りを無音で埋める
            for (; i < samples; i++) buffer[i] = 0;
            for (int v = 0; v < HAL_SOUND_VOICES; v++) {
                cur_freq[v]    = 0.0f;
                voice_phase[v] = 0.0f;
            }
            return;
        }

        // このバッファで、現在のステップに使えるサンプル数
        uint32_t n = (uint32_t)(samples - i);
        if (n > step_samples_left) n = step_samples_left;

        for (uint32_t k = 0; k < n; k++, i++) {
            float mixed = 0.0f;

            for (int v = 0; v < HAL_SOUND_VOICES; v++) {
                if (cur_freq[v] <= 0.0f) continue;

                // 矩形波（PSG 音色）: 前半周期で +amp、後半周期で -amp
                mixed += (voice_phase[v] < 0.5f) ? cur_amp[v] : -cur_amp[v];

                voice_phase[v] += cur_freq[v] / (float)SAMPLE_RATE;
                if (voice_phase[v] >= 1.0f) voice_phase[v] -= 1.0f;
            }

            if (mixed > 32767.0f) mixed = 32767.0f;
            if (mixed < -32768.0f) mixed = -32768.0f;

            int16_t sample = (int16_t)mixed;
            buffer[i] = ((uint32_t)(uint16_t)sample << 16) | (uint32_t)(uint16_t)sample;
        }

        step_samples_left -= n;
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
        cur_freq[v]    = 0.0f;
        cur_amp[v]     = 0.0f;
    }
    queue_head = 0;
    queue_tail = 0;
    step_samples_left = 0;
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
}

void hal_sound_play_voices(const HalSoundVoice voices[HAL_SOUND_VOICES], int duration_ms) {
    if (duration_ms <= 0) return;

    // 満杯のときだけ待つ。長い曲でキューを無制限に伸ばさないため
    while ((uint32_t)(queue_tail - queue_head) >= SOUND_QUEUE_SIZE) {
        tight_loop_contents();
    }

    SoundStep* step = &sound_queue[queue_tail & (SOUND_QUEUE_SIZE - 1)];
    for (int v = 0; v < HAL_SOUND_VOICES; v++) {
        step->freq[v]   = (voices[v].frequency > 0) ? voices[v].frequency : 0.0f;
        step->volume[v] = voices[v].volume;
    }
    step->samples = (uint32_t)duration_ms * SAMPLES_PER_MS;

    __dmb(); // 中身を書き終えてから tail を進める
    queue_tail = queue_tail + 1;
}

void hal_sound_stop() {
    // 実際の停止は次のバッファ生成時（最大 5.3ms 後）に割り込み側が行う
    stop_requested = true;
}

int hal_sound_is_playing() {
    if (stop_requested) return 0;
    return (queue_head != queue_tail || step_samples_left > 0) ? 1 : 0;
}

void hal_sound_startup_chime() {
    // PC-9801 の起動音「ピポッ」に倣った 2 音（2000Hz → 1000Hz）
    hal_sound_play(2000.0f, 100);
    hal_sound_play(1000.0f, 100);
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

int hal_sound_is_playing() {
    // ホストでは即時に鳴り終わった扱い
    return 0;
}

void hal_sound_startup_chime() {
    printf("[Sound] STARTUP CHIME (2000Hz -> 1000Hz)\n");
}

void hal_sound_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 15) volume = 15;
    current_volume = volume;
    printf("[Sound] Volume set to %d\n", current_volume);
}

#endif

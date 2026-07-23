#include "hal_sound.h"
#include "psg_envelope.h"
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
// 2 のべき乗であること。1 音 = 音符 + 切れ目で 2 ステップ使うので、
// 256 段でおよそ 128 音まで待たずに積める。これを超える曲は、
// キューが空くまで PLAY がブロックする（曲は正しく鳴るが、その間 BASIC は止まる）。
#define SOUND_QUEUE_SIZE 256

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

// ---------------------------------------------------------
// PSG（AY-3-8910 相当）
//
//   psg_reg  … メインが SOUND で書き、割り込みが読む（volatile）
//   psg_mode … true の間はキューではなく PSG レジスタで合成する
// 位相・ノイズ LFSR は割り込みだけが触る
// ---------------------------------------------------------
#define PSG_CLOCK_HZ 2000000.0f // X1 相当

static volatile uint8_t psg_reg[HAL_SOUND_PSG_REGS];
static volatile bool    psg_mode = false;

static float    psg_tone_phase[3] = {0.0f, 0.0f, 0.0f};
static float    psg_noise_phase   = 0.0f;
static uint32_t psg_lfsr = 1;   // ノイズ生成用 17bit LFSR
static int      psg_noise_bit = 1;

// エンベロープ状態（割り込みのみ）。振幅と方向を直接持つ
static float psg_env_phase = 0.0f;   // 0.0〜1.0 で 1 段進む
static int   psg_env_vol   = 15;     // 現在の振幅 0〜15
static int   psg_env_dir   = -1;     // +1 上昇 / -1 下降
static bool  psg_env_hold  = false;  // 端で保持中
static uint8_t psg_env_shape_prev = 0xFF; // R13 書き込み検出用

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

// PSG レジスタから 1 バッファ分を合成する。
// パラメータはバッファ先頭で 1 回読み、位相・ノイズは 1 サンプルごとに進める。
static void fill_psg(uint32_t* buffer, int samples) {
    float tone_inc[3];
    int   vol[3];
    bool  tone_en[3], noise_en[3], both_off[3], use_env[3];

    for (int ch = 0; ch < 3; ch++) {
        int period = psg_reg[2 * ch] | ((psg_reg[2 * ch + 1] & 0x0F) << 8);
        if (period < 1) period = 1;
        float freq = PSG_CLOCK_HZ / (16.0f * (float)period);
        tone_inc[ch]  = freq / (float)SAMPLE_RATE;
        tone_en[ch]   = !(psg_reg[7] & (1 << ch));        // R7 bit ch: 0=有効
        noise_en[ch]  = !(psg_reg[7] & (1 << (ch + 3)));  // R7 bit ch+3
        both_off[ch]  = !tone_en[ch] && !noise_en[ch];
        vol[ch]       = psg_reg[8 + ch] & 0x0F;
        use_env[ch]   = psg_reg[8 + ch] & 0x10;           // bit4: エンベロープ使用
    }

    int nperiod = psg_reg[6] & 0x1F;
    if (nperiod < 1) nperiod = 1;
    float noise_inc = (PSG_CLOCK_HZ / (16.0f * (float)nperiod)) / (float)SAMPLE_RATE;

    // エンベロープ周期。R13 が書き換わったらサイクルを頭から始める
    uint8_t shape = psg_reg[13] & 0x0F;
    if (shape != psg_env_shape_prev) {
        psg_env_shape_prev = shape;
        psg_env_phase = 0.0f;
        psg_envelope_init(&psg_env_vol, &psg_env_dir, &psg_env_hold, shape);
    }
    int eperiod = psg_reg[11] | (psg_reg[12] << 8);
    if (eperiod < 1) eperiod = 1;
    // 1 ステップ（16 段のうち 1 段）あたりの進み。周期 = 2MHz/(256*EP) の 16 倍細かい
    float env_inc = (PSG_CLOCK_HZ / (256.0f * (float)eperiod)) / (float)SAMPLE_RATE;

    for (int i = 0; i < samples; i++) {
        // ノイズ LFSR（AY-3-8910: 17bit、bit0 と bit3 の XOR を帰還）
        psg_noise_phase += noise_inc;
        if (psg_noise_phase >= 1.0f) {
            psg_noise_phase -= 1.0f;
            int fb = (psg_lfsr ^ (psg_lfsr >> 3)) & 1;
            psg_lfsr = (psg_lfsr >> 1) | ((uint32_t)fb << 16);
            psg_noise_bit = psg_lfsr & 1;
        }

        // エンベロープを進める
        psg_env_phase += env_inc;
        while (psg_env_phase >= 1.0f) {
            psg_env_phase -= 1.0f;
            psg_envelope_step(&psg_env_vol, &psg_env_dir, &psg_env_hold, psg_reg[13] & 0x0F);
        }
        float env = (float)psg_env_vol / 15.0f;

        float mixed = 0.0f;
        for (int ch = 0; ch < 3; ch++) {
            if (both_off[ch]) continue; // トーンもノイズも無効 → 無音（DC を出さない）

            psg_tone_phase[ch] += tone_inc[ch];
            if (psg_tone_phase[ch] >= 1.0f) psg_tone_phase[ch] -= 1.0f;
            int tone_out = (psg_tone_phase[ch] < 0.5f) ? 1 : 0;

            // AY ミキサー: (トーン or 無効) AND (ノイズ or 無効)
            int t = tone_en[ch]  ? tone_out      : 1;
            int n = noise_en[ch] ? psg_noise_bit : 1;

            // 音量: bit4 が立っていればエンベロープ、そうでなければ固定
            float scale = use_env[ch] ? env : ((float)vol[ch] / 15.0f);
            float amp = VOICE_MAX_AMPLITUDE * scale;
            mixed += (t & n) ? amp : -amp; // 0 中心の矩形波で DC を避ける
        }

        if (mixed > 32767.0f) mixed = 32767.0f;
        if (mixed < -32768.0f) mixed = -32768.0f;
        int16_t sample = (int16_t)mixed;
        buffer[i] = ((uint32_t)(uint16_t)sample << 16) | (uint32_t)(uint16_t)sample;
    }
}

static void fill_audio_buffer(uint32_t* buffer, int samples) {
    int i = 0;

    if (psg_mode) {
        fill_psg(buffer, samples);
        return;
    }

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

    psg_mode = false; // キュー再生に戻す（PSG と排他）

    // 満杯のときだけ待つ。長い曲でキューを無制限に伸ばさないため。
    // 停止要求が来たら積むのをやめて抜ける（満杯待ちで固まらないように）
    while ((uint32_t)(queue_tail - queue_head) >= SOUND_QUEUE_SIZE) {
        if (stop_requested) return;
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
    psg_mode = false;
    stop_requested = true;
}

int hal_sound_is_playing() {
    if (psg_mode) return 1; // PSG は明示停止まで鳴り続ける
    if (stop_requested) return 0;
    return (queue_head != queue_tail || step_samples_left > 0) ? 1 : 0;
}

void hal_sound_psg_write(int reg, int data) {
    if (reg < 0 || reg >= HAL_SOUND_PSG_REGS) return;

    if (!psg_mode) {
        // キュー再生から PSG へ切替。レジスタと LFSR を初期化する
        for (int i = 0; i < HAL_SOUND_PSG_REGS; i++) psg_reg[i] = 0;
        psg_lfsr = 1;
        psg_noise_bit = 1;
    }
    psg_reg[reg] = (uint8_t)(data & 0xFF);
    psg_mode = true; // 最後に立てる（それまで割り込みはキュー側を見る）
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

void hal_sound_psg_write(int reg, int data) {
    printf("[Sound] PSG R%d = %d\n", reg, data & 0xFF);
}

#endif

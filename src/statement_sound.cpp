#include "parser_internal.h"
#include "hal_display.h"
#include "hal_sound.h"
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>

// 音を鳴らす文。
// BEEP / SOUND（PSG レジスタ直叩き）と、MML を解釈する MUSIC / PLAY。

void execute_beep(const TokenList&, int& pos) {
    pos++; 
    hal_sound_beep();
}

// SOUND reg, data : PSG（AY-3-8910 相当）レジスタへの書き込み。
// レジスタの意味は hal_sound.h を参照。周波数 = 2MHz / (16 × 周期)。
void execute_sound(const TokenList& tokens, int& pos) {
    pos++;
    Value reg_val = parse_relation(tokens, pos);
    require_token(tokens, pos, TokenType::COMMA, "Expected ',' in SOUND"); pos++;
    Value data_val = parse_relation(tokens, pos);

    int reg = static_cast<int>(reg_val.num_val);
    if (reg < 0 || reg >= HAL_SOUND_PSG_REGS)
        throw std::runtime_error("Illegal function call: SOUND register 0-15");

    hal_sound_psg_write(reg, static_cast<int>(data_val.num_val));
}

// ---------------------------------------------------------
// MML (MUSIC / PLAY)
//
// MML 文字列は一度「発音イベント列」に変換してから再生する。
// こうすることで最大 HAL_SOUND_VOICES 本のトラックを
// 時間軸上で合成し、PSG 相当の和音として鳴らせる。
// ---------------------------------------------------------

#define MAX_MML_EVENTS 256

// 1 音分の発音イベント（freq <= 0 は休符）
// duration_ms の末尾 gap_ms は消音する。こうしないと同じ高さの音が
// 連続したときに 1 つの長い音として繋がって聞こえてしまう
struct MmlEvent {
    float freq;
    float duration_ms;
    float gap_ms;
    int   volume;
};

struct MmlTrack {
    MmlEvent events[MAX_MML_EVENTS];
    int      count;
};

// ctype に渡す前に unsigned char へ直す。
//
// char が符号付きの処理系では、半角カタカナ（0xA1-0xDF）のような 0x80 以上の
// バイトが負の値として渡り未定義動作になる。ARM の char は符号なしなので実機は
// 素通りし、ホストのテストだけが踏む。lexer.cpp と同じ流儀に揃える
static inline int uch(char c) { return (unsigned char)c; }

// MML の命令に続く 10 進数を読む。O / L / T / V と音長の 5 か所で使う。
// 桁が増え続けても int を溢れさせないよう、上限で頭打ちにする
// （どの命令も範囲外は呼び出し側が弾くので、頭打ちの値がそのまま使われることはない）
static int read_mml_number(const char* mml, int& i, int fallback) {
    if (!std::isdigit(uch(mml[i]))) return fallback;

    constexpr int LIMIT = 100000;
    int val = 0;
    while (std::isdigit(uch(mml[i]))) {
        if (val < LIMIT) val = val * 10 + (mml[i] - '0');
        i++;
    }
    return val;
}

// MML 文字列を解釈してイベント列に変換する（この時点では発音しない）
static void parse_mml(const char* mml, MmlTrack& track) {
    track.count = 0;

    int octave = 4;
    int default_len = 4;
    int tempo = 120;
    int volume = HAL_SOUND_DEFAULT_VOLUME;

    int i = 0;
    while (mml[i] != '\0') {
        char c = (char)std::toupper(uch(mml[i++]));
        if (std::isspace(uch(c))) continue;

        switch (c) {
            case 'O': {
                int val = read_mml_number(mml, i, octave);
                if (val >= 1 && val <= 8) octave = val;
                break;
            }
            case 'L': {
                int val = read_mml_number(mml, i, default_len);
                if (val >= 1 && val <= 64) default_len = val;
                break;
            }
            case 'T': {
                int val = read_mml_number(mml, i, tempo);
                if (val >= 32 && val <= 255) tempo = val;
                break;
            }
            case 'V': {
                int val = read_mml_number(mml, i, volume);
                if (val < 0) val = 0;
                if (val > 15) val = 15;
                volume = val;
                break;
            }
            case '>': octave++; if (octave > 8) octave = 8; break;
            case '<': octave--; if (octave < 1) octave = 1; break;

            case 'C': case 'D': case 'E': case 'F': case 'G': case 'A': case 'B':
            case 'R': {
                int note = 0;
                bool is_rest = (c == 'R');
                if (!is_rest) {
                    if (c == 'C') note = 0;
                    else if (c == 'D') note = 2;
                    else if (c == 'E') note = 4;
                    else if (c == 'F') note = 5;
                    else if (c == 'G') note = 7;
                    else if (c == 'A') note = 9;
                    else if (c == 'B') note = 11;

                    if (mml[i] == '#' || mml[i] == '+') { note++; i++; }
                    else if (mml[i] == '-') { note--; i++; }
                }

                int len = read_mml_number(mml, i, default_len);
                if (len <= 0) len = default_len;

                float duration_ms = (60.0f / tempo) * (4.0f / len) * 1000.0f;
                if (mml[i] == '.') {
                    duration_ms *= 1.5f;
                    i++;
                }

                if (track.count >= MAX_MML_EVENTS) {
                    throw std::runtime_error("MML too long");
                }

                MmlEvent& ev = track.events[track.count++];
                ev.duration_ms = duration_ms;
                ev.volume      = volume;
                ev.freq = is_rest
                    ? 0.0f
                    : 440.0f * powf(2.0f, (note - 9 + (octave - 4) * 12) / 12.0f);
                // 音符の末尾に短い切れ目を入れる（休符には不要）
                // 常に音長より短くなるよう割合で頭打ちにする
                ev.gap_ms = is_rest
                    ? 0.0f
                    : (duration_ms * 0.15f < 10.0f ? duration_ms * 0.15f : 10.0f);
                break;
            }
        }
    }
}

// 複数トラックを時間軸上で合成して再生する。
// 「次にどれかのトラックの音が変わるまで」を 1 ステップとして同時発音する。
static void play_mml_tracks(const MmlTrack* tracks, int track_count) {
    int   index[HAL_SOUND_VOICES]     = {0, 0, 0};
    float remaining[HAL_SOUND_VOICES] = {0.0f, 0.0f, 0.0f};

    for (int v = 0; v < track_count; v++) {
        if (tracks[v].count > 0) remaining[v] = tracks[v].events[0].duration_ms;
    }

    // ステップ長を ms 単位に丸める際の誤差でテンポがずれないよう、
    // 「本来の経過時間」と「実際に発音した時間」の差を持ち越す
    float elapsed_target = 0.0f;
    int   elapsed_actual = 0;

    while (true) {
        // 残っているトラックのうち、最も早く音が切り替わる時間を求める。
        // 音符の途中でも、末尾の切れ目に入る瞬間が切り替わり点になる
        float step = -1.0f;
        for (int v = 0; v < track_count; v++) {
            if (index[v] >= tracks[v].count) continue;
            const MmlEvent& ev = tracks[v].events[index[v]];
            float next_change = (remaining[v] > ev.gap_ms)
                ? (remaining[v] - ev.gap_ms) // 発音中 → 切れ目の開始まで
                : remaining[v];              // 切れ目中 → 次の音符まで
            if (step < 0.0f || next_change < step) step = next_change;
        }
        if (step < 0.0f) break; // 全トラック終了

        HalSoundVoice voices[HAL_SOUND_VOICES] = {};
        for (int v = 0; v < track_count; v++) {
            if (index[v] >= tracks[v].count) continue;
            const MmlEvent& ev = tracks[v].events[index[v]];
            // 音符の末尾の切れ目に入っている声は消音する
            voices[v].frequency = (remaining[v] > ev.gap_ms) ? ev.freq : 0.0f;
            voices[v].volume    = ev.volume;
        }

        elapsed_target += step;
        int step_ms = (int)(elapsed_target - (float)elapsed_actual);
        if (step_ms < 0) step_ms = 0;
        elapsed_actual += step_ms;

        hal_sound_play_voices(voices, step_ms);

        // 鳴らした分だけ各トラックを進める
        for (int v = 0; v < track_count; v++) {
            if (index[v] >= tracks[v].count) continue;
            remaining[v] -= step;
            if (remaining[v] <= 0.0f) {
                index[v]++;
                if (index[v] < tracks[v].count) {
                    remaining[v] = tracks[v].events[index[v]].duration_ms;
                }
            }
        }
    }
    // ここで hal_sound_stop() は呼ばない。
    // 積んだキューを捨ててしまい、非同期再生が鳴る前に消える
}

void execute_music(const TokenList& tokens, int& pos) {
    pos++;

    static MmlTrack tracks[HAL_SOUND_VOICES]; // 約 9KB。スタックを避けて静的に確保する
    int track_count = 0;

    // MUSIC "..." [, "..." [, "..."]] — カンマ区切りで最大 3 声
    while (true) {
        Value mml_val = parse_relation(tokens, pos);
        if (mml_val.type != Value::Type::STR) throw std::runtime_error("Type Mismatch: MUSIC expects string");

        if (track_count >= HAL_SOUND_VOICES) {
            throw std::runtime_error("Too many voices: MUSIC supports up to 3");
        }
        parse_mml(mml_val.str_val, tracks[track_count++]);

        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
            pos++;
            continue;
        }
        break;
    }

    play_mml_tracks(tracks, track_count);
}

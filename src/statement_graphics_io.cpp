#include "parser_internal.h"
#include "hal_display.h"
#include "hal_gpio.h"
#include "hal_sdcard.h"
#include "hal_sound.h"

#include "hal_touch.h"
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>

void execute_wait(const TokenList& tokens, int& pos) {
    pos++; 
    Value val = parse_relation(tokens, pos);
    hal_system_wait(static_cast<int>(val.num_val));
}

void execute_locate(const TokenList& tokens, int& pos) {
    pos++; 
    Value vx = parse_relation(tokens, pos);
    require_token(tokens, pos, TokenType::COMMA, "Expected ','");
    pos++;
    Value vy = parse_relation(tokens, pos);
    hal_display_locate(static_cast<int>(vx.num_val), static_cast<int>(vy.num_val));
}

void execute_color(const TokenList& tokens, int& pos) {
    pos++; 
    Value val = parse_relation(tokens, pos);
    if ((val.type != Value::Type::NUM && val.type != Value::Type::INT) && val.type != Value::Type::INT) 
        throw std::runtime_error("Type Mismatch: COLOR expects number");
    int idx = static_cast<int>(val.num_val);
    if (idx < 0 || idx > 15) throw std::runtime_error("Invalid color index (0-15)");
    current_color_565 = PALETTE[idx];
}

void execute_pset(const TokenList& tokens, int& pos) {
    pos++; 
    Value vx = parse_relation(tokens, pos);
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
    Value vy = parse_relation(tokens, pos);
    
    uint16_t color = current_color_565;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
        pos++;
        Value vc = parse_relation(tokens, pos);
        int idx = static_cast<int>(vc.num_val);
        if (idx >= 0 && idx <= 15) color = PALETTE[idx];
    }
    hal_graphics_pset(static_cast<int>(vx.num_val), static_cast<int>(vy.num_val), color);
}

void execute_line(const TokenList& tokens, int& pos) {
    pos++; 
    Value vx1 = parse_relation(tokens, pos);
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
    Value vy1 = parse_relation(tokens, pos);
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
    Value vx2 = parse_relation(tokens, pos);
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
    Value vy2 = parse_relation(tokens, pos);
    
    uint16_t color = current_color_565;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
        pos++;
        Value vc = parse_relation(tokens, pos);
        int idx = static_cast<int>(vc.num_val);
        if (idx >= 0 && idx <= 15) color = PALETTE[idx];
    }
    hal_graphics_line(static_cast<int>(vx1.num_val), static_cast<int>(vy1.num_val), 
                      static_cast<int>(vx2.num_val), static_cast<int>(vy2.num_val), color);
}

void execute_circle(const TokenList& tokens, int& pos) {
    pos++; 
    Value vx = parse_relation(tokens, pos);
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
    Value vy = parse_relation(tokens, pos);
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
    Value vr = parse_relation(tokens, pos);
    
    uint16_t color = current_color_565;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
        pos++;
        Value vc = parse_relation(tokens, pos);
        int idx = static_cast<int>(vc.num_val);
        if (idx >= 0 && idx <= 15) color = PALETTE[idx];
    }
    hal_graphics_circle(static_cast<int>(vx.num_val), static_cast<int>(vy.num_val), 
                        static_cast<int>(vr.num_val), color);
}

void execute_gpio(const TokenList& tokens, int& pos) {
    pos++; 
    Value vpin = parse_relation(tokens, pos);
    require_token(tokens, pos, TokenType::COMMA, "Expected ','");
    pos++;
    Value vmode = parse_relation(tokens, pos);
    
    int pin = static_cast<int>(vpin.num_val);
    int mode = static_cast<int>(vmode.num_val); 
    int value = 0;
    int pull = 0;

    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
        pos++;
        Value vval = parse_relation(tokens, pos);
        value = static_cast<int>(vval.num_val);
    }
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
        pos++;
        Value vpull = parse_relation(tokens, pos);
        pull = static_cast<int>(vpull.num_val);
    }

    hal_gpio_init(pin, mode, pull);
    if (mode == 1) { 
        hal_gpio_write(pin, value != 0);
    }
}

void execute_brightness(const TokenList& tokens, int& pos) {
    pos++; 
    Value level = parse_relation(tokens, pos);
    hal_display_set_brightness(static_cast<int>(level.num_val));
}

void execute_paint(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::LPAREN, "Expected '('"); pos++;
    Value vx = parse_relation(tokens, pos);
    require_token(tokens, pos, TokenType::COMMA, "Expected ','"); pos++;
    Value vy = parse_relation(tokens, pos);
    require_token(tokens, pos, TokenType::RPAREN, "Expected ')'"); pos++;
    require_token(tokens, pos, TokenType::COMMA, "Expected ','"); pos++;
    Value vc = parse_relation(tokens, pos);
    
    int x = static_cast<int>(vx.num_val);
    int y = static_cast<int>(vy.num_val);
    int color_idx = static_cast<int>(vc.num_val);
    if (color_idx < 0 || color_idx > 15) throw std::runtime_error("Invalid color index");
    uint16_t fill_color = PALETTE[color_idx];
    
    uint16_t border_color = 0xFFFF;
    bool stop_at_border = false;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
        pos++;
        Value vb = parse_relation(tokens, pos);
        int b_idx = static_cast<int>(vb.num_val);
        if (b_idx >= 0 && b_idx <= 15) {
            border_color = PALETTE[b_idx];
            stop_at_border = true;
        }
    }

    uint16_t target_color = hal_graphics_get_pixel(x, y);
    if (target_color == fill_color) return;
    if (stop_at_border && target_color == border_color) return;

    struct Point { int x, y; };
    static Point stack[4096];
    int sp = 0;
    
    auto push = [&](int px, int py) {
        if (sp < 4096 && px >= 0 && px < 320 && py >= 0 && py < 240) {
            if (hal_graphics_get_pixel(px, py) == target_color) {
                stack[sp++] = {px, py};
            }
        }
    };
    
    push(x, y);
    
    while (sp > 0) {
        Point p = stack[--sp];
        int px = p.x;
        int py = p.y;
        
        if (hal_graphics_get_pixel(px, py) != target_color) continue;
        
        int lx = px;
        while (lx > 0 && hal_graphics_get_pixel(lx - 1, py) == target_color) lx--;
        int rx = px;
        while (rx < 319 && hal_graphics_get_pixel(rx + 1, py) == target_color) rx++;
        
        for (int i = lx; i <= rx; i++) {
            hal_graphics_pset(i, py, fill_color);
        }
        
        for (int i = lx; i <= rx; i++) {
            if (py > 0 && hal_graphics_get_pixel(i, py - 1) == target_color) {
                if (i == lx || hal_graphics_get_pixel(i - 1, py - 1) != target_color) {
                    push(i, py - 1);
                }
            }
            if (py < 239 && hal_graphics_get_pixel(i, py + 1) == target_color) {
                if (i == lx || hal_graphics_get_pixel(i - 1, py + 1) != target_color) {
                    push(i, py + 1);
                }
            }
        }
    }
    
    hal_display_sync();
}

void execute_get_at(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::LPAREN, "Expected '('"); pos++;
    Value vx1 = parse_relation(tokens, pos);
    require_token(tokens, pos, TokenType::COMMA, "Expected ','"); pos++;
    Value vy1 = parse_relation(tokens, pos);
    require_token(tokens, pos, TokenType::RPAREN, "Expected ')'"); pos++;
    require_token(tokens, pos, TokenType::MINUS, "Expected '-'"); pos++;
    require_token(tokens, pos, TokenType::LPAREN, "Expected '('"); pos++;
    Value vx2 = parse_relation(tokens, pos);
    require_token(tokens, pos, TokenType::COMMA, "Expected ','"); pos++;
    Value vy2 = parse_relation(tokens, pos);
    require_token(tokens, pos, TokenType::RPAREN, "Expected ')'"); pos++;
    require_token(tokens, pos, TokenType::COMMA, "Expected ','"); pos++;
    
    if (pos >= tokens.size || tokens.tokens[pos].type != TokenType::IDENTIFIER)
        throw std::runtime_error("Expected array name");
    
    char array_name[64];
    strncpy(array_name, tokens.tokens[pos].text, sizeof(array_name)-1);
    pos++;
    
    ArrayRef* arr = get_array(array_name);
    if (!arr) throw std::runtime_error("Array not dimensioned");
    
    int x1 = static_cast<int>(vx1.num_val), y1 = static_cast<int>(vy1.num_val);
    int x2 = static_cast<int>(vx2.num_val), y2 = static_cast<int>(vy2.num_val);
    int w = abs(x2 - x1) + 1, h = abs(y2 - y1) + 1;
    
    if (2 + w * h > arr->total_size()) throw std::runtime_error("Array too small for image");
    
    write_heap_value(arr->start_addr, Value((float)w));
    write_heap_value(arr->start_addr + 8, Value((float)h));
    
    int idx = 2;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            uint16_t color = hal_graphics_get_pixel(x1 + i, y1 + j);
            write_heap_value(arr->start_addr + (idx++ * 8), Value((float)color));
        }
    }
}

void execute_put_at(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::LPAREN, "Expected '('"); pos++;
    Value vx = parse_relation(tokens, pos);
    require_token(tokens, pos, TokenType::COMMA, "Expected ','"); pos++;
    Value vy = parse_relation(tokens, pos);
    require_token(tokens, pos, TokenType::RPAREN, "Expected ')'"); pos++;
    require_token(tokens, pos, TokenType::COMMA, "Expected ','"); pos++;
    
    if (pos >= tokens.size || tokens.tokens[pos].type != TokenType::IDENTIFIER)
        throw std::runtime_error("Expected array name");
    
    char array_name[64];
    strncpy(array_name, tokens.tokens[pos].text, sizeof(array_name)-1);
    pos++;
    
    ArrayRef* arr = get_array(array_name);
    if (!arr) throw std::runtime_error("Array not dimensioned");
    
    int w = static_cast<int>(read_heap_value(arr->start_addr).num_val);
    int h = static_cast<int>(read_heap_value(arr->start_addr + 8).num_val);
    int px = static_cast<int>(vx.num_val), py = static_cast<int>(vy.num_val);
    
    int idx = 2;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            uint16_t color = static_cast<uint16_t>(read_heap_value(arr->start_addr + (idx++ * 8)).num_val);
            hal_graphics_pset(px + i, py + j, color);
        }
    }
    hal_display_sync();
}

void execute_save(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::STRING, "Syntax Error: SAVE expects filename string");
    const char* filename = tokens.tokens[pos].text;
    pos++;
    
    void* fp = hal_file_open(filename, "w");
    if (!fp) throw std::runtime_error("File Error: Cannot open file for writing");
    
    uint16_t ptr = MEMORY_TEXT_BASE;
    while (true) {
        if (logical_memory[ptr+2] == 0 && logical_memory[ptr+3] == 0 && ptr != MEMORY_TEXT_BASE) break;

        uint16_t line_num = logical_memory[ptr+2] | (logical_memory[ptr+3] << 8);
        hal_file_printf(fp, "%d ", line_num);
        
        TokenList t = get_detokenized_line(ptr); // from program_manager.cpp
        for (int i=0; i<t.size; i++) {
            if (t.tokens[i].type == TokenType::END_OF_FILE) break;
            if (t.tokens[i].type == TokenType::STRING) hal_file_printf(fp, "\"%s\" ", t.tokens[i].text);
            else hal_file_printf(fp, "%s ", t.tokens[i].text);
        }
        hal_file_printf(fp, "\n");
        
        uint16_t next_ptr = logical_memory[ptr] | (logical_memory[ptr+1] << 8);
        if (next_ptr == 0) break;
        ptr = next_ptr;
    }
    hal_file_close(fp);
    basic_print("Saved\n");
}

void execute_load(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::STRING, "Syntax Error: LOAD expects filename string");
    const char* filename = tokens.tokens[pos].text;
    pos++;
    
    void* fp = hal_file_open(filename, "r");
    if (!fp) throw std::runtime_error("File Error: Cannot open file for reading");
    
    clear_program();
    char line_buf[256];
    while (hal_file_gets(line_buf, sizeof(line_buf), fp)) {
        TokenList t = lex(line_buf);
        if (t.size > 0 && t.tokens[0].type == TokenType::NUMBER) {
            int line_num = atoi(t.tokens[0].text);
            TokenList remainder;
            int j = 0;
            for (int i = 1; i < t.size; i++) {
                if (t.tokens[i].type == TokenType::END_OF_FILE) break;
                remainder.tokens[j++] = t.tokens[i];
            }
            remainder.size = j;
            store_line(line_num, remainder);
        }
    }
    hal_file_close(fp);
    basic_print("Loaded\n");
}

void execute_kill(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::STRING, "Syntax Error: KILL expects filename string");
    const char* filename = tokens.tokens[pos].text;
    pos++;
    
    if (hal_file_remove(filename) != 0) {
        throw std::runtime_error("File Error: Cannot delete file");
    }
    basic_print("Deleted\n");
}

void execute_name(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::STRING, "Syntax Error: NAME expects old filename string");
    char oldname[128];
    strncpy(oldname, tokens.tokens[pos].text, sizeof(oldname)-1);
    pos++;
    
    require_token(tokens, pos, TokenType::AS, "Syntax Error: Expected AS in NAME command");
    pos++;
    
    require_token(tokens, pos, TokenType::STRING, "Syntax Error: NAME expects new filename string");
    const char* newname = tokens.tokens[pos].text;
    pos++;
    
    if (hal_file_rename(oldname, newname) != 0) {
        throw std::runtime_error("File Error: Cannot rename file");
    }
    basic_print("Renamed\n");
}

void execute_files(const TokenList& tokens, int& pos) {
    pos++; 
    
    void* dir = hal_dir_open(".");
    if (dir == NULL) {
        basic_print("Error: Cannot open directory\n");
        return;
    }
    
    // 桁幅 16 × 2 列 = 32 桁。LCD は 40 桁なので途中で折り返さない
    const int COL_WIDTH = 16;
    const int COLS_PER_ROW = 2;

    const char* d_name;
    int count = 0;      // 見つかったファイル数
    int col = 0;        // 現在の行に出した個数
    char buf[128];

    while ((d_name = hal_dir_read(dir)) != NULL) {
        count++;

        if (strlen(d_name) >= (size_t)COL_WIDTH) {
            // 桁に収まらない長い名前は独立した行に出す。
            // 詰めて出すと隣のファイル名と繋がって読めなくなるため
            if (col != 0) {
                basic_print("\n");
                col = 0;
            }
            snprintf(buf, sizeof(buf), "%s\n", d_name);
            basic_print(buf);
            continue;
        }

        snprintf(buf, sizeof(buf), "%-*s", COL_WIDTH, d_name);
        basic_print(buf);
        if (++col >= COLS_PER_ROW) {
            basic_print("\n");
            col = 0;
        }
    }
    if (col != 0) basic_print("\n");

    hal_dir_close(dir);
    snprintf(buf, sizeof(buf), "%d File(s) found\n", count);
    basic_print(buf);
}

void execute_beep(const TokenList& tokens, int& pos) {
    pos++; 
    hal_sound_beep();
}

void execute_sound(const TokenList& tokens, int& pos) {
    pos++; 
    Value freq_val = parse_relation(tokens, pos);
    require_token(tokens, pos, TokenType::COMMA, "Expected ','"); pos++;
    Value dur_val = parse_relation(tokens, pos);
    
    hal_sound_play(freq_val.num_val, static_cast<int>(dur_val.num_val));
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

// MML 文字列を解釈してイベント列に変換する（この時点では発音しない）
static void parse_mml(const char* mml, MmlTrack& track) {
    track.count = 0;

    int octave = 4;
    int default_len = 4;
    int tempo = 120;
    int volume = 15;

    int i = 0;
    while (mml[i] != '\0') {
        char c = std::toupper(mml[i++]);
        if (std::isspace(c)) continue;

        switch (c) {
            case 'O': {
                int val = 0;
                while (std::isdigit(mml[i])) val = val * 10 + (mml[i++] - '0');
                if (val >= 1 && val <= 8) octave = val;
                break;
            }
            case 'L': {
                int val = 0;
                while (std::isdigit(mml[i])) val = val * 10 + (mml[i++] - '0');
                if (val >= 1 && val <= 64) default_len = val;
                break;
            }
            case 'T': {
                int val = 0;
                while (std::isdigit(mml[i])) val = val * 10 + (mml[i++] - '0');
                if (val >= 32 && val <= 255) tempo = val;
                break;
            }
            case 'V': {
                int val = 0;
                while (std::isdigit(mml[i])) val = val * 10 + (mml[i++] - '0');
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

                int len = default_len;
                if (std::isdigit(mml[i])) {
                    len = 0;
                    while (std::isdigit(mml[i])) len = len * 10 + (mml[i++] - '0');
                }
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

    hal_sound_stop();
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

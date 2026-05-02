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
    hal_display_print("Saved\n");
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
    hal_display_print("Loaded\n");
}

void execute_kill(const TokenList& tokens, int& pos) {
    pos++; 
    require_token(tokens, pos, TokenType::STRING, "Syntax Error: KILL expects filename string");
    const char* filename = tokens.tokens[pos].text;
    pos++;
    
    if (hal_file_remove(filename) != 0) {
        throw std::runtime_error("File Error: Cannot delete file");
    }
    hal_display_print("Deleted\n");
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
    hal_display_print("Renamed\n");
}

void execute_files(const TokenList& tokens, int& pos) {
    pos++; 
    
    void* dir = hal_dir_open(".");
    if (dir == NULL) {
        hal_display_print("Error: Cannot open directory\n");
        return;
    }
    
    const char* d_name;
    int count = 0;
    char buf[128];
    while ((d_name = hal_dir_read(dir)) != NULL) {
        if (d_name[0] == '.') continue;
        
        snprintf(buf, sizeof(buf), "%-16s", d_name);
        hal_display_print(buf);
        count++;
        if (count % 4 == 0) hal_display_print("\n");
    }
    if (count % 4 != 0) hal_display_print("\n");
    
    hal_dir_close(dir);
    snprintf(buf, sizeof(buf), "%d File(s) found\n", count);
    hal_display_print(buf);
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

void execute_music(const TokenList& tokens, int& pos) {
    pos++; 
    Value mml_val = parse_relation(tokens, pos);
    if (mml_val.type != Value::Type::STR) throw std::runtime_error("Type Mismatch: MUSIC expects string");
    
    const char* mml = mml_val.str_val;
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
                hal_sound_set_volume(val);
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
                
                float duration_ms = (60.0f / tempo) * (4.0f / len) * 1000.0f;
                if (mml[i] == '.') {
                    duration_ms *= 1.5f;
                    i++;
                }
                
                if (is_rest) {
                    hal_sound_play(0, (int)duration_ms);
                } else {
                    float freq = 440.0f * powf(2.0f, (note - 9 + (octave - 4) * 12) / 12.0f);
                    hal_sound_play(freq, (int)duration_ms);
                }
                break;
            }
        }
    }
}

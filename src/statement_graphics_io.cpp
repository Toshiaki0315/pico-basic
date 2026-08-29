#include "parser_internal.h"
#include "hal_display.h"
#include "hal_gpio.h"
#include "hal_sdcard.h"
#include "hal_sound.h"

#include "hal_touch.h"
#include "hal_battery.h"
#include "parser.h"
#include "line_input.h"
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>

// ---------------------------------------------------------
// WINDOW: ユーザー座標系
//
//   WINDOW (xs,ys)-(xe,ye), (x1,y1)-(x2,y2)
//     画面上の矩形 (xs,ys)-(xe,ye) に、ユーザー座標 (x1,y1)-(x2,y2) を割り当てる。
//   WINDOW
//     既定（画面座標そのまま）に戻す。
//
// 以降 PSET / LINE / CIRCLE / PAINT / POLY の座標はユーザー座標として扱う。
// ---------------------------------------------------------
static bool  window_active = false;
static float window_screen_x0 = 0.0f, window_screen_y0 = 0.0f;
static float window_screen_x1 = 0.0f, window_screen_y1 = 0.0f;
static float window_user_x0 = 0.0f, window_user_y0 = 0.0f;
static float window_user_x1 = 0.0f, window_user_y1 = 0.0f;

void reset_graphics_window() {
    window_active = false;
}

// ユーザー座標 → 画面座標
void user_to_screen(float ux, float uy, int& out_x, int& out_y) {
    if (!window_active) {
        out_x = (int)ux;
        out_y = (int)uy;
        return;
    }

    float ux_span = window_user_x1 - window_user_x0;
    float uy_span = window_user_y1 - window_user_y0;
    // 幅ゼロの指定でゼロ除算しないよう保険
    float sx = (ux_span == 0.0f) ? window_screen_x0
             : window_screen_x0 + (ux - window_user_x0)
                                  * (window_screen_x1 - window_screen_x0) / ux_span;
    float sy = (uy_span == 0.0f) ? window_screen_y0
             : window_screen_y0 + (uy - window_user_y0)
                                  * (window_screen_y1 - window_screen_y0) / uy_span;
    out_x = (int)sx;
    out_y = (int)sy;
}

// 半径など「長さ」の変換。X 方向の倍率を使う
static int user_to_screen_length(float length) {
    if (!window_active) return (int)length;

    float ux_span = window_user_x1 - window_user_x0;
    if (ux_span == 0.0f) return (int)length;

    float scale = (window_screen_x1 - window_screen_x0) / ux_span;
    if (scale < 0.0f) scale = -scale;
    return (int)(length * scale);
}

// 座標を 1 組読む。Hu-BASIC 本来の `(x,y)` と、括弧を省いた `x,y` の
// どちらでも書けるようにする（既存プログラム・テストは括弧なしを使っている）。
// 読み取るのはユーザー座標なので、実数のまま返す
static void parse_point_user(const TokenList& tokens, int& pos, float& out_x, float& out_y) {
    bool parenthesized = (pos < tokens.size && tokens.tokens[pos].type == TokenType::LPAREN);
    if (parenthesized) pos++;

    Value vx = parse_relation(tokens, pos);
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
    Value vy = parse_relation(tokens, pos);

    if (parenthesized) {
        require_token(tokens, pos, TokenType::RPAREN, "Expected ')' after coordinates");
        pos++;
    }

    out_x = vx.num_val;
    out_y = vy.num_val;
}

// 座標を 1 組読み、画面座標に変換して返す
static void parse_point(const TokenList& tokens, int& pos, int& out_x, int& out_y) {
    float ux, uy;
    parse_point_user(tokens, pos, ux, uy);
    user_to_screen(ux, uy, out_x, out_y);
}

// 色指定（省略時は現在の COLOR）を読む
static uint16_t parse_optional_color(const TokenList& tokens, int& pos) {
    uint16_t color = current_color_565;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
        pos++;
        Value vc = parse_relation(tokens, pos);
        int idx = static_cast<int>(vc.num_val);
        if (idx >= 0 && idx <= 15) color = PALETTE[idx];
    }
    return color;
}

// 長い待ちの間も電源ボタンを効かせたいので、まとめて眠らずに細かく刻んで、
// その合間にボタンを見る。塞がっている間は誰もボタンを見られないため。
// 刻み幅は、反応の速さと刻むこと自体が積む誤差との兼ね合いで決めた
#define WAIT_SLICE_MS 50

void execute_wait(const TokenList& tokens, int& pos) {
    pos++;
    Value val = parse_relation(tokens, pos);
    int remain = static_cast<int>(val.num_val);

    while (remain > 0) {
        int slice = (remain < WAIT_SLICE_MS) ? remain : WAIT_SLICE_MS;
        hal_system_wait(slice);
        remain -= slice;
        if (hal_battery_power_key_held()) {
            basic_power_off(); // 落ちれば戻らない。戻ったら実行は既に止まっている
            return;
        }
    }
}

// CONSOLE ys, yl : テキストのスクロール領域を行 ys から yl 行分に制限する。
// Hu-BASIC の CONSOLE は引数が多いが、本実装では開始行と行数だけを解釈する。
// 引数なしの CONSOLE で全画面に戻す。
void execute_console(const TokenList& tokens, int& pos) {
    pos++;

    if (pos >= tokens.size || tokens.tokens[pos].type == TokenType::END_OF_FILE
        || tokens.tokens[pos].type == TokenType::COLON) {
        hal_display_set_scroll_region(0, hal_display_text_rows() - 1);
        return;
    }

    int top = static_cast<int>(parse_relation(tokens, pos).num_val);

    int rows = hal_display_text_rows() - top; // 省略時は下端まで
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
        pos++;
        rows = static_cast<int>(parse_relation(tokens, pos).num_val);
    }

    if (top < 0 || rows <= 0 || top >= hal_display_text_rows())
        throw std::runtime_error("Illegal function call: CONSOLE range");

    int bottom = top + rows - 1;
    if (bottom > hal_display_text_rows() - 1) bottom = hal_display_text_rows() - 1;

    hal_display_set_scroll_region(top, bottom);
    // カーソルを領域の先頭へ移動する。これをしないとカーソルが領域外に
    // 残り、そこに文字が出続けて「CONSOLE が効かない」ように見える
    hal_display_locate(0, top);
}

// WIDTH c, l : 文字数・行数の指定。
// 本実装のフォントは 8x8 固定（40x30）なので、指定値がそれと一致するかだけ
// 検査し、異なる場合はエラーにする（黙って無視して混乱させない）。
void execute_width(const TokenList& tokens, int& pos) {
    pos++;

    if (pos >= tokens.size || tokens.tokens[pos].type == TokenType::END_OF_FILE
        || tokens.tokens[pos].type == TokenType::COLON) {
        return; // 引数なしは現状維持
    }

    int cols = static_cast<int>(parse_relation(tokens, pos).num_val);
    if (cols != hal_display_text_cols())
        throw std::runtime_error("Illegal function call: WIDTH only supports 40 columns");

    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
        pos++;
        int rows = static_cast<int>(parse_relation(tokens, pos).num_val);
        if (rows != hal_display_text_rows())
            throw std::runtime_error("Illegal function call: WIDTH only supports 30 rows");
    }
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
    if (!val.is_numeric() && val.type != Value::Type::INT) 
        throw std::runtime_error("Type Mismatch: COLOR expects number");
    int idx = static_cast<int>(val.num_val);
    if (idx < 0 || idx > 15) throw std::runtime_error("Invalid color index (0-15)");
    current_color_565 = PALETTE[idx];
}

void execute_window(const TokenList& tokens, int& pos) {
    pos++;

    // 引数なしの WINDOW は既定（画面座標そのまま）へ戻す
    if (pos >= tokens.size || tokens.tokens[pos].type == TokenType::END_OF_FILE
        || tokens.tokens[pos].type == TokenType::COLON) {
        reset_graphics_window();
        return;
    }

    // WINDOW (xs,ys)-(xe,ye), (x1,y1)-(x2,y2)
    // 変換前に読む必要があるため parse_point_user() を使う
    float sx0, sy0, sx1, sy1;
    parse_point_user(tokens, pos, sx0, sy0);
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::MINUS) pos++;
    else if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
    parse_point_user(tokens, pos, sx1, sy1);

    require_token(tokens, pos, TokenType::COMMA, "Expected ',' before user coordinates");
    pos++;

    float ux0, uy0, ux1, uy1;
    parse_point_user(tokens, pos, ux0, uy0);
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::MINUS) pos++;
    else if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
    parse_point_user(tokens, pos, ux1, uy1);

    if (ux0 == ux1 || uy0 == uy1)
        throw std::runtime_error("Illegal function call: WINDOW user range is empty");

    window_screen_x0 = sx0; window_screen_y0 = sy0;
    window_screen_x1 = sx1; window_screen_y1 = sy1;
    window_user_x0   = ux0; window_user_y0   = uy0;
    window_user_x1   = ux1; window_user_y1   = uy1;
    window_active = true;
}

void execute_pset(const TokenList& tokens, int& pos) {
    pos++;
    int x, y;
    parse_point(tokens, pos, x, y);          // PSET (x,y), c  /  PSET x,y,c
    uint16_t color = parse_optional_color(tokens, pos);

    hal_graphics_pset(x, y, color);
    // フレームバッファに書くだけでは画面に出ないので、描いた分を転送する
    hal_display_sync_rect(x, y, 1, 1);
}

void execute_line(const TokenList& tokens, int& pos) {
    pos++;
    int x1, y1, x2, y2;
    parse_point(tokens, pos, x1, y1);        // LINE (x1,y1)-(x2,y2), c  /  LINE x1,y1,x2,y2,c

    // 括弧付きの書式では 2 点を '-' で繋ぐ。括弧なしなら ',' 区切り
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::MINUS) pos++;
    else if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;

    parse_point(tokens, pos, x2, y2);
    uint16_t color = parse_optional_color(tokens, pos);

    // 末尾の B（矩形）/ BF（塗りつぶし矩形）
    bool box = false, box_fill = false;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA
        && pos + 1 < tokens.size && tokens.tokens[pos + 1].type == TokenType::IDENTIFIER) {
        const char* opt = tokens.tokens[pos + 1].text;
        if (strcasecmp(opt, "B") == 0) {
            box = true;  pos += 2;
        } else if (strcasecmp(opt, "BF") == 0) {
            box = box_fill = true; pos += 2;
        }
    }

    int lx = (x1 < x2) ? x1 : x2;
    int ly = (y1 < y2) ? y1 : y2;
    int rx = (x1 > x2) ? x1 : x2;
    int ry = (y1 > y2) ? y1 : y2;

    if (box_fill) {
        for (int y = ly; y <= ry; y++) {
            hal_graphics_line(lx, y, rx, y, color);
        }
    } else if (box) {
        hal_graphics_line(lx, ly, rx, ly, color); // 上辺
        hal_graphics_line(lx, ry, rx, ry, color); // 下辺
        hal_graphics_line(lx, ly, lx, ry, color); // 左辺
        hal_graphics_line(rx, ly, rx, ry, color); // 右辺
    } else {
        hal_graphics_line(x1, y1, x2, y2, color);
    }

    // 描いた範囲を囲む矩形だけを転送する
    hal_display_sync_rect(lx, ly, rx - lx + 1, ry - ly + 1);
}

void execute_circle(const TokenList& tokens, int& pos) {
    pos++;
    int cx, cy;
    parse_point(tokens, pos, cx, cy);        // CIRCLE (x,y), r, c  /  CIRCLE x,y,r,c

    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
    Value vr = parse_relation(tokens, pos);
    // 半径もユーザー座標なので、WINDOW の倍率を掛ける
    int r = user_to_screen_length(vr.num_val);

    uint16_t color = parse_optional_color(tokens, pos);

    hal_graphics_circle(cx, cy, r, color);
    hal_display_sync_rect(cx - r, cy - r, 2 * r + 1, 2 * r + 1);
}

// POLY (x,y), r, c, n [, skip [, angle]]
//   (x,y) 中心 / r 外接円の半径 / c 色 / n 頂点数（3 以上）
//   skip  何個先の頂点と結ぶか。1 = 多角形、2 以上 = 星形（既定 1）
//   angle 最初の頂点の角度（度、既定 -90 = 真上）
//
// Hu-BASIC の POLY は引数の意味が資料で確定できなかったため、
// 本実装ではこの並びを正とする（MANUAL / specification.md に明記）。
void execute_poly(const TokenList& tokens, int& pos) {
    pos++;

    float ucx, ucy;
    parse_point_user(tokens, pos, ucx, ucy);

    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
    Value vr = parse_relation(tokens, pos);

    uint16_t color = parse_optional_color(tokens, pos);

    int vertices = 3;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
        pos++;
        vertices = static_cast<int>(parse_relation(tokens, pos).num_val);
    }
    int skip = 1;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
        pos++;
        skip = static_cast<int>(parse_relation(tokens, pos).num_val);
    }
    float start_angle = -90.0f;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
        pos++;
        start_angle = parse_relation(tokens, pos).num_val;
    }

    if (vertices < 3) throw std::runtime_error("Illegal function call: POLY needs 3 or more vertices");
    if (skip < 1 || skip >= vertices)
        throw std::runtime_error("Illegal function call: POLY skip out of range");

    int cx, cy;
    user_to_screen(ucx, ucy, cx, cy);
    int r = user_to_screen_length(vr.num_val);
    if (r <= 0) return;

    const float PI_F = 3.14159265358979323846f;
    float step = 2.0f * PI_F / (float)vertices;
    float base = start_angle * PI_F / 180.0f;

    // skip 個先の頂点と順に結ぶ。頂点数と skip が互いに素でない場合は
    // 一周では戻ってこないので、全頂点を通るまで開始点をずらす
    bool visited[64] = {false};
    int max_vertices = (vertices < 64) ? vertices : 64;

    for (int origin = 0; origin < max_vertices; origin++) {
        if (visited[origin]) continue;

        int current = origin;
        do {
            visited[current] = true;
            int next = (current + skip) % vertices;

            int x1 = cx + (int)(r * cosf(base + step * current));
            int y1 = cy + (int)(r * sinf(base + step * current));
            int x2 = cx + (int)(r * cosf(base + step * next));
            int y2 = cy + (int)(r * sinf(base + step * next));
            hal_graphics_line(x1, y1, x2, y2, color);

            current = next;
        } while (current != origin && current < max_vertices && !visited[current]);
    }

    hal_display_sync_rect(cx - r, cy - r, 2 * r + 1, 2 * r + 1);
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
    
    // 溢れたら黙って塗り残すのではなく、後で利用者に知らせる
    bool stack_overflowed = false;
    // 画面サイズを直書きすると別解像度のボードで壊れるので HAL から取る
    int scr_w, scr_h;
    hal_display_get_info(scr_w, scr_h);
    auto push = [&](int px, int py) {
        if (px < 0 || px >= scr_w || py < 0 || py >= scr_h) return;
        if (hal_graphics_get_pixel(px, py) != target_color) return;

        if (sp >= 4096) {
            stack_overflowed = true;
            return;
        }
        stack[sp++] = {px, py};
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
        while (rx < scr_w - 1 && hal_graphics_get_pixel(rx + 1, py) == target_color) rx++;
        
        for (int i = lx; i <= rx; i++) {
            hal_graphics_pset(i, py, fill_color);
        }
        
        for (int i = lx; i <= rx; i++) {
            if (py > 0 && hal_graphics_get_pixel(i, py - 1) == target_color) {
                if (i == lx || hal_graphics_get_pixel(i - 1, py - 1) != target_color) {
                    push(i, py - 1);
                }
            }
            if (py < scr_h - 1 && hal_graphics_get_pixel(i, py + 1) == target_color) {
                if (i == lx || hal_graphics_get_pixel(i - 1, py + 1) != target_color) {
                    push(i, py + 1);
                }
            }
        }
    }
    
    hal_display_sync();

    if (stack_overflowed) {
        basic_print("Paint incomplete: region too complex\n");
    }
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
    
    // 他の描画命令と同じく WINDOW の座標系に従う
    int x1, y1, x2, y2;
    user_to_screen(vx1.num_val, vy1.num_val, x1, y1);
    user_to_screen(vx2.num_val, vy2.num_val, x2, y2);
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

// PUT@ の描画モード（本実装の定義。MANUAL / specification に明記）
//   0 PSET   … そのまま上書き（既定）
//   1 OR     … 画面と論理和
//   2 AND    … 画面と論理積
//   3 XOR    … 画面と排他的論理和（同じ絵を 2 回置くと消える＝スプライト向き）
//   4 PRESET … 色を反転して上書き
enum { PUT_PSET = 0, PUT_OR = 1, PUT_AND = 2, PUT_XOR = 3, PUT_PRESET = 4 };

static uint16_t apply_put_mode(int mode, uint16_t src, uint16_t dst) {
    switch (mode) {
        case PUT_OR:     return src | dst;
        case PUT_AND:    return src & dst;
        case PUT_XOR:    return src ^ dst;
        case PUT_PRESET: return (uint16_t)~src;
        case PUT_PSET:
        default:         return src;
    }
}

// PUT@ (x,y), A [, mode]
// PUT@ (x1,y1)-(x2,y2), A [, mode]   … 転送先矩形に合わせて拡大縮小する
void execute_put_at(const TokenList& tokens, int& pos) {
    pos++;

    int px1, py1;
    parse_point(tokens, pos, px1, py1);

    // 第 2 点があれば拡大縮小、なければ等倍
    bool has_dst_rect = false;
    int px2 = px1, py2 = py1;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::MINUS) {
        pos++;
        parse_point(tokens, pos, px2, py2);
        has_dst_rect = true;
    }

    require_token(tokens, pos, TokenType::COMMA, "Expected ',' before array name"); pos++;

    if (pos >= tokens.size || tokens.tokens[pos].type != TokenType::IDENTIFIER)
        throw std::runtime_error("Expected array name");
    char array_name[64];
    strncpy(array_name, tokens.tokens[pos].text, sizeof(array_name) - 1);
    array_name[sizeof(array_name) - 1] = '\0';
    pos++;

    int mode = PUT_PSET;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) {
        pos++;
        mode = static_cast<int>(parse_relation(tokens, pos).num_val);
    }

    ArrayRef* arr = get_array(array_name);
    if (!arr) throw std::runtime_error("Array not dimensioned");

    int w = static_cast<int>(read_heap_value(arr->start_addr).num_val);
    int h = static_cast<int>(read_heap_value(arr->start_addr + 8).num_val);
    if (w <= 0 || h <= 0) return;

    // 転送先のサイズ。矩形指定がなければ元画像と同じ
    int dst_w = has_dst_rect ? (abs(px2 - px1) + 1) : w;
    int dst_h = has_dst_rect ? (abs(py2 - py1) + 1) : h;
    int ox = (px1 < px2) ? px1 : px2;
    int oy = (py1 < py2) ? py1 : py2;

    for (int dy = 0; dy < dst_h; dy++) {
        for (int dx = 0; dx < dst_w; dx++) {
            // 転送先ピクセル → 元画像の対応ピクセル（最近傍）
            int si = has_dst_rect ? (dx * w / dst_w) : dx;
            int sj = has_dst_rect ? (dy * h / dst_h) : dy;

            uint16_t src = static_cast<uint16_t>(
                read_heap_value(arr->start_addr + ((2 + sj * w + si) * 8)).num_val);

            int x = ox + dx;
            int y = oy + dy;
            uint16_t color = (mode == PUT_PSET)
                ? src
                : apply_put_mode(mode, src, hal_graphics_get_pixel(x, y));
            hal_graphics_pset(x, y, color);
        }
    }

    hal_display_sync_rect(ox, oy, dst_w, dst_h);
}

// ---------------------------------------------------------
// シーケンシャルファイル I/O（OPEN / PRINT# / INPUT# / EOF / CLOSE）
//
// PRINT # は値を "," 区切りの 1 行として書き、INPUT # はカンマまたは
// 行末で区切られたフィールドを読む。この対で往復できる。
// ---------------------------------------------------------
struct BasicFile {
    void* fp;
    int   mode;          // 0=未使用 1=INPUT 2=OUTPUT/APPEND
    char  linebuf[160];  // INPUT# 用の行バッファ
    int   linepos;
    bool  line_valid;
};
static BasicFile basic_files[MAX_BASIC_FILES + 1]; // 添字 1〜4 を使う

static BasicFile& file_slot(int n) {
    if (n < 1 || n > MAX_BASIC_FILES)
        throw std::runtime_error("Illegal function call: file number must be 1-4");
    return basic_files[n];
}

// `#n` を読む（# は省略可）
static int parse_file_number(const TokenList& tokens, int& pos) {
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::HASH) pos++;
    Value v = parse_relation(tokens, pos);
    if (!v.is_numeric()) throw std::runtime_error("Type Mismatch: file number");
    return (int)v.num_val;
}

void basic_files_close_all() {
    for (int i = 1; i <= MAX_BASIC_FILES; i++) {
        if (basic_files[i].mode != 0 && basic_files[i].fp) hal_file_close(basic_files[i].fp);
        basic_files[i].mode = 0;
        basic_files[i].fp = nullptr;
        basic_files[i].line_valid = false;
    }
}

// OPEN "ファイル名" FOR INPUT|OUTPUT|APPEND AS #n
void execute_open(const TokenList& tokens, int& pos) {
    pos++; // OPEN
    Value fname = parse_relation(tokens, pos);
    if (fname.type != Value::Type::STR) throw std::runtime_error("Type Mismatch: OPEN expects filename string");

    require_token(tokens, pos, TokenType::FOR, "Syntax Error: Expected FOR in OPEN"); pos++;
    const char* fmode;
    int mode;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::INPUT) {
        fmode = "r"; mode = 1; pos++;
    } else if (pos < tokens.size && tokens.tokens[pos].type == TokenType::IDENTIFIER &&
               strcmp(tokens.tokens[pos].text, "OUTPUT") == 0) {
        fmode = "w"; mode = 2; pos++;
    } else if (pos < tokens.size && tokens.tokens[pos].type == TokenType::IDENTIFIER &&
               strcmp(tokens.tokens[pos].text, "APPEND") == 0) {
        fmode = "a"; mode = 2; pos++;
    } else {
        throw std::runtime_error("Syntax Error: Expected INPUT, OUTPUT or APPEND");
    }
    require_token(tokens, pos, TokenType::AS, "Syntax Error: Expected AS in OPEN"); pos++;

    int n = parse_file_number(tokens, pos);
    BasicFile& f = file_slot(n);
    if (f.mode != 0) throw std::runtime_error("File already open");

    f.fp = hal_file_open(fname.str_val, fmode);
    if (!f.fp) throw std::runtime_error("File not found");
    f.mode = mode;
    f.line_valid = false;
    f.linepos = 0;
}

// CLOSE [#n [, #m ...]] — 引数なしは開いている全ファイルを閉じる
void execute_close(const TokenList& tokens, int& pos) {
    pos++; // CLOSE
    if (pos >= tokens.size || tokens.tokens[pos].type == TokenType::END_OF_FILE ||
        tokens.tokens[pos].type == TokenType::COLON || tokens.tokens[pos].type == TokenType::REM) {
        basic_files_close_all();
        return;
    }
    while (true) {
        int n = parse_file_number(tokens, pos);
        BasicFile& f = file_slot(n);
        if (f.mode != 0 && f.fp) hal_file_close(f.fp);
        f.mode = 0;
        f.fp = nullptr;
        f.line_valid = false;
        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) { pos++; continue; }
        break;
    }
}

// PRINT #n, 式 [{,|;} 式 ...] — 値を "," 区切りで 1 行に書く
void execute_print_file(const TokenList& tokens, int& pos) {
    int n = parse_file_number(tokens, pos); // pos は HASH を指して呼ばれる
    BasicFile& f = file_slot(n);
    if (f.mode == 0) throw std::runtime_error("File not open");
    if (f.mode != 2) throw std::runtime_error("Bad file mode");

    char line[256] = "";
    bool first = true;
    while (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE &&
           tokens.tokens[pos].type != TokenType::COLON &&
           tokens.tokens[pos].type != TokenType::REM) {
        if (tokens.tokens[pos].type == TokenType::COMMA ||
            tokens.tokens[pos].type == TokenType::SEMICOLON) { pos++; continue; }
        Value v = parse_relation(tokens, pos);
        if (!first) strncat(line, ",", sizeof(line) - strlen(line) - 1);
        strncat(line, v.c_str(), sizeof(line) - strlen(line) - 1);
        first = false;
    }
    if (hal_file_printf(f.fp, "%s\n", line) < 0)
        throw std::runtime_error("File Error: write failed");
}

// 1 フィールド読む（カンマまたは行末区切り）。EOF なら false
static bool file_read_field(BasicFile& f, char* out, int maxlen) {
    if (!f.line_valid) {
        if (!hal_file_gets(f.linebuf, sizeof(f.linebuf), f.fp)) return false;
        f.linepos = 0;
        f.line_valid = true;
    }
    while (f.linebuf[f.linepos] == ' ') f.linepos++;
    int o = 0;
    while (true) {
        char c = f.linebuf[f.linepos];
        if (c == '\0' || c == '\n' || c == '\r') { f.line_valid = false; break; }
        f.linepos++;
        if (c == ',') break;
        if (o < maxlen - 1) out[o++] = c;
    }
    out[o] = '\0';
    while (o > 0 && out[o - 1] == ' ') out[--o] = '\0'; // 末尾の空白を除去
    return true;
}

// INPUT #n, 変数 [, 変数 ...]
// 1 行をそのまま読む（カンマで区切らない）。
// INPUT # が途中まで読んだ行が残っていれば、その残りを返す
static bool file_read_line_raw(BasicFile& f, char* out, int maxlen) {
    if (!f.line_valid) {
        if (!hal_file_gets(f.linebuf, sizeof(f.linebuf), f.fp)) return false;
        f.linepos = 0;
        f.line_valid = true;
    }
    int o = 0;
    while (true) {
        char c = f.linebuf[f.linepos];
        if (c == '\0' || c == '\n' || c == '\r') break;
        f.linepos++;
        if (o < maxlen - 1) out[o++] = c;
    }
    out[o] = '\0';
    f.line_valid = false; // 1 行使い切った
    return true;
}

// LINE INPUT [#n,] 変数$ / LINE INPUT "プロンプト"; 変数$
//
// `INPUT #` はカンマで区切って読むため、カンマを含む文字列は復元できない。
// こちらは 1 行をそのまま 1 個の文字列として読む。
// コンソールから読む場合、`INPUT` と違ってプロンプトを指定しなければ "? " も出さない。
void execute_line_input(const TokenList& tokens, int& pos) {
    pos++; // LINE
    pos++; // INPUT

    bool from_file = false;
    int  file_no = 0;
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::HASH) {
        file_no = parse_file_number(tokens, pos);
        from_file = true;
        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
    } else if (pos < tokens.size && tokens.tokens[pos].type == TokenType::STRING) {
        basic_print(tokens.tokens[pos].text);
        pos++;
        if (pos < tokens.size && (tokens.tokens[pos].type == TokenType::COMMA ||
                                  tokens.tokens[pos].type == TokenType::SEMICOLON)) pos++;
    }

    require_token(tokens, pos, TokenType::IDENTIFIER,
                  "Syntax Error: LINE INPUT expects a string variable");
    char var_name[64];
    strncpy(var_name, tokens.tokens[pos].text, sizeof(var_name) - 1);
    var_name[sizeof(var_name) - 1] = '\0';
    pos++;

    int nlen = (int)strlen(var_name);
    if (nlen == 0 || var_name[nlen - 1] != '$')
        throw std::runtime_error("Type Mismatch: LINE INPUT needs a string variable");

    int arr_idx, arr_idx2;
    parse_optional_indices(tokens, pos, arr_idx, arr_idx2);

    char buf[256] = "";
    if (from_file) {
        BasicFile& f = file_slot(file_no);
        if (f.mode == 0) throw std::runtime_error("File not open");
        if (f.mode != 1) throw std::runtime_error("Bad file mode");
        if (!file_read_line_raw(f, buf, sizeof(buf)))
            throw std::runtime_error("Input past end of file");
    } else {
        // 長押しで中断されたときは既にプログラムが止まっている（line_input.h 参照）
        if (!line_input_read_line(buf, sizeof(buf))) return;
    }

    Value val(buf);
    if (arr_idx >= 0) {
        ArrayRef* arr = get_array(var_name);
        if (!arr) throw std::runtime_error("Array not dimensioned");
        int flat_idx = flatten_array_index(arr, arr_idx, arr_idx2);
        write_heap_value(arr->start_addr + (flat_idx * 8), val);
    } else {
        set_variable(var_name, val);
    }
}

void execute_input_file(const TokenList& tokens, int& pos) {
    int n = parse_file_number(tokens, pos); // pos は HASH を指して呼ばれる
    BasicFile& f = file_slot(n);
    if (f.mode == 0) throw std::runtime_error("File not open");
    if (f.mode != 1) throw std::runtime_error("Bad file mode");
    if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;

    while (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE) {
        require_token(tokens, pos, TokenType::IDENTIFIER, "Syntax Error: INPUT# expects identifier");
        char var_name[64];
        strncpy(var_name, tokens.tokens[pos].text, sizeof(var_name) - 1);
        var_name[sizeof(var_name) - 1] = '\0';
        pos++;

        int arr_idx, arr_idx2;
        parse_optional_indices(tokens, pos, arr_idx, arr_idx2);

        char field[128];
        if (!file_read_field(f, field, sizeof(field)))
            throw std::runtime_error("Input past end of file");

        int nlen = strlen(var_name);
        bool is_str_var = (nlen > 0 && var_name[nlen - 1] == '$');
        bool is_int_var = (nlen > 0 && var_name[nlen - 1] == '%');
        Value val;
        if (is_str_var)      val = Value(field);
        else if (is_int_var) val = Value((int)atof(field));
        else                 val = Value((float)atof(field));

        if (arr_idx >= 0) {
            ArrayRef* arr = get_array(var_name);
            if (!arr) throw std::runtime_error("Array not dimensioned");
            int flat_idx = flatten_array_index(arr, arr_idx, arr_idx2);
            write_heap_value(arr->start_addr + (flat_idx * 8), val);
        } else {
            set_variable(var_name, val);
        }

        if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
        else break;
    }
}

// EOF(n): これ以上読むものが無ければ 1。次の行を先読みしてバッファに保持する
int basic_file_eof(int fileno) {
    BasicFile& f = file_slot(fileno);
    if (f.mode == 0) throw std::runtime_error("File not open");
    if (f.mode != 1) return 1; // 書き込み用は常に終端扱い

    if (f.line_valid) {
        // 現在行に空白以外の未読があれば「まだある」
        int p = f.linepos;
        while (f.linebuf[p] == ' ') p++;
        if (f.linebuf[p] != '\0' && f.linebuf[p] != '\n' && f.linebuf[p] != '\r') return 0;
        f.line_valid = false; // 空白だけなら行を使い切ったとみなす
    }
    if (!hal_file_gets(f.linebuf, sizeof(f.linebuf), f.fp)) return 1;
    f.linepos = 0;
    f.line_valid = true;
    return 0;
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

        uint16_t line_num = prog_line_no(ptr);
        hal_file_printf(fp, "%d ", line_num);
        
        TokenList t = get_detokenized_line(ptr); // from program_manager.cpp
        for (int i=0; i<t.size; i++) {
            if (t.tokens[i].type == TokenType::END_OF_FILE) break;
            if (t.tokens[i].type == TokenType::STRING) hal_file_printf(fp, "\"%s\" ", t.tokens[i].text);
            else hal_file_printf(fp, "%s ", t.tokens[i].text);
        }
        hal_file_printf(fp, "\n");
        
        uint16_t next_ptr = prog_next_ptr(ptr);
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

// MML 文字列を解釈してイベント列に変換する（この時点では発音しない）
static void parse_mml(const char* mml, MmlTrack& track) {
    track.count = 0;

    int octave = 4;
    int default_len = 4;
    int tempo = 120;
    int volume = HAL_SOUND_DEFAULT_VOLUME;

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

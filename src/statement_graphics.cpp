#include "parser_internal.h"
#include "parser.h"
#include "hal_display.h"
#include "hal_gpio.h"
#include "hal_battery.h"
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>

// 画面と入出力ピンを触る文。
// 座標系（WINDOW）、テキストの位置と色（CONSOLE / WIDTH / LOCATE / COLOR）、
// 図形（PSET / LINE / CIRCLE / POLY / PAINT / GET@ / PUT@）、
// GPIO / BRIGHTNESS、それに WAIT。
//
// ファイル I/O は statement_file_io.cpp、音は statement_sound.cpp にある。

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
    copy_string(array_name, sizeof(array_name), tokens.tokens[pos].text);
    pos++;
    
    ArrayRef* arr = get_array(array_name);
    if (!arr) throw std::runtime_error("Array not dimensioned");
    
    // 他の描画命令と同じく WINDOW の座標系に従う
    int x1, y1, x2, y2;
    user_to_screen(vx1.num_val, vy1.num_val, x1, y1);
    user_to_screen(vx2.num_val, vy2.num_val, x2, y2);
    int w = abs(x2 - x1) + 1, h = abs(y2 - y1) + 1;

    // 掛ける前に、辺の長さだけを先に確かめる。
    //
    // user_to_screen() は座標をクランプしないので、w と h はいくらでも大きくなる。
    // 先に w * h を計算すると int が溢れ、`GET@ (0,0)-(65535,65535), A` では
    // 65536 * 65536 が 0 に折り返して下の「配列が小さい」判定を素通りしてしまう。
    // 素通りした先のループは write_heap_value() を 43 億回呼び、アドレスが
    // uint16_t で折り返しながらプログラム本文まで上書きする。
    //
    // GET@ は画面の内容を取る命令なので、画面より大きい矩形に意味は無い
    int scr_w, scr_h;
    hal_display_get_info(scr_w, scr_h);
    if (w > scr_w || h > scr_h)
        throw std::runtime_error("Illegal function call: GET@ rectangle is larger than the screen");

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
    copy_string(array_name, sizeof(array_name), tokens.tokens[pos].text);
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
    if (w <= 0 || h <= 0) return; // GET@ していない配列。描くものが無い

    // 画像の寸法は配列の先頭 2 要素そのものなので、BASIC から書き換えられる。
    // GET@ が作れない大きさが入っていたら信じない。信じると戻ってこない
    // （A(0)=30000 : A(1)=30000 で 9 億回のループになる）
    int scr_w, scr_h;
    hal_display_get_info(scr_w, scr_h);
    if (w > scr_w || h > scr_h)
        throw std::runtime_error("Illegal function call: PUT@ image size is not valid");

    // 転送先のサイズ。矩形指定がなければ元画像と同じ
    int dst_w = has_dst_rect ? (abs(px2 - px1) + 1) : w;
    int dst_h = has_dst_rect ? (abs(py2 - py1) + 1) : h;
    // 転送先も同じ理由で確かめる。画面より広い矩形へ拡大しても描く先が無い
    if (dst_w > scr_w || dst_h > scr_h)
        throw std::runtime_error("Illegal function call: PUT@ destination is larger than the screen");
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

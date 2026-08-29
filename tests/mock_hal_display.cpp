#include "hal_display.h"
#include "hal_gpio.h"
#include "hal_touch.h"
#include "mock_hal_display.h"
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>

// ---------------------------------------------------------
// Implementation of Mock API
// ---------------------------------------------------------
static std::vector<std::string> printed_lines;
static std::string raw_buffer;
static std::vector<DrawCommand> draw_commands;
static std::vector<mock_hal::GpioCommand> gpio_commands;
static std::vector<int> brightness_levels;
static uint16_t mock_frame_buffer[320 * 240];
static bool cls_called = false;

namespace mock_hal {

std::vector<std::string> get_printed_lines() {
    return printed_lines;
}

std::string get_raw_print_buffer() {
    return raw_buffer;
}

std::vector<DrawCommand> get_draw_commands() {
    return draw_commands;
}

std::vector<GpioCommand> get_gpio_commands() {
    return gpio_commands;
}

std::vector<int> get_brightness_levels() {
    return brightness_levels;
}

void reset() {
    printed_lines.clear();
    raw_buffer.clear();
    draw_commands.clear();
    gpio_commands.clear();
    brightness_levels.clear();
    memset(mock_frame_buffer, 0, sizeof(mock_frame_buffer));
    cls_called = false;
}

bool was_cls_called() {
    return cls_called;
}

} // namespace mock_hal

// ---------------------------------------------------------
// Implementation of real HAL API used by the Parser/Engine
// ---------------------------------------------------------

void hal_display_init() {
    // No-op for host tests
}

void hal_display_print(const char* text) {
    raw_buffer += text;
    printed_lines.push_back(std::string(text));
}

void hal_display_cls() {
    memset(mock_frame_buffer, 0, sizeof(mock_frame_buffer));
    cls_called = true;
}

static int mock_cursor_x = 0;
static int mock_cursor_y = 0;

void hal_display_locate(int x, int y) {
    if (x >= 0) mock_cursor_x = x;
    if (y >= 0) mock_cursor_y = y;
}

namespace mock_hal {
    int get_cursor_x() { return mock_cursor_x; }
    int get_cursor_y() { return mock_cursor_y; }
}

void hal_graphics_pset(int x, int y, uint16_t color) {
    if (x >= 0 && x < 320 && y >= 0 && y < 240) {
        mock_frame_buffer[y * 320 + x] = color;
    }
    draw_commands.push_back({DrawCommand::PSET, x, y, 0, 0, 0, color});
}

uint16_t hal_graphics_get_pixel(int x, int y) {
    if (x < 0 || x >= 320 || y < 0 || y >= 240) return 0;
    return mock_frame_buffer[y * 320 + x];
}

void hal_display_sync() {
    // No-op for mock
}

void hal_display_sync_rect(int x, int y, int w, int h) {
    // No-op for mock（実機では指定矩形だけを SPI 転送する）
    (void)x; (void)y; (void)w; (void)h;
}

// 転送の遅延。ホストには LCD が無いので状態だけ持ち、テストから確認できるようにする
static bool mock_deferred = false;
static int  mock_flush_count = 0;

void hal_display_set_deferred(bool deferred) {
    if (!deferred) hal_display_flush();
    mock_deferred = deferred;
}
bool hal_display_is_deferred() { return mock_deferred; }
void hal_display_flush() { mock_flush_count++; }

namespace mock_hal {
int  get_flush_count() { return mock_flush_count; }
void reset_flush_count() { mock_flush_count = 0; }
}

void hal_graphics_line(int x1, int y1, int x2, int y2, uint16_t color) {
    // Record high-level command for existing tests
    draw_commands.push_back({DrawCommand::LINE, x1, y1, x2, y2, 0, color});
    
    // Update buffer for advanced commands (without recording each pset)
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy, e2;
    while (true) {
        if (x1 >= 0 && x1 < 320 && y1 >= 0 && y1 < 240) mock_frame_buffer[y1 * 320 + x1] = color;
        if (x1 == x2 && y1 == y2) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void hal_graphics_circle(int x0, int y0, int radius, uint16_t color) {
    // Record high-level command
    draw_commands.push_back({DrawCommand::CIRCLE, x0, y0, 0, 0, radius, color});
    
    // Update buffer
    int x = radius, y = 0;
    int err = 0;
    while (x >= y) {
        auto set = [&](int px, int py) {
            if (px >= 0 && px < 320 && py >= 0 && py < 240) mock_frame_buffer[py * 320 + px] = color;
        };
        set(x0 + x, y0 + y); set(x0 + y, y0 + x);
        set(x0 - y, y0 + x); set(x0 - x, y0 + y);
        set(x0 - x, y0 - y); set(x0 - y, y0 - x);
        set(x0 + y, y0 - x); set(x0 + x, y0 - y);
        if (err <= 0) { y += 1; err += 2 * y + 1; }
        if (err > 0) { x -= 1; err -= 2 * x + 1; }
    }
}

void hal_display_get_info(int& width, int& height) {
    width = 320;
    height = 240;
}

void hal_display_set_brightness(int level) {
    brightness_levels.push_back(level);
}

void hal_gpio_init(int pin, int mode, int pull) {
    gpio_commands.push_back({pin, mode, pull, false});
}

void hal_gpio_write(int pin, bool value) {
    for (auto& cmd : gpio_commands) {
        if (cmd.pin == pin) {
            cmd.value = value;
            return;
        }
    }
    gpio_commands.push_back({pin, 1, 0, value}); // Default to output if not init
}

bool hal_gpio_read(int pin) {
    for (auto& cmd : gpio_commands) {
        if (cmd.pin == pin) return cmd.value;
    }
    return false;
}

static char mock_input_buf[256] = "";
static int  mock_line_pos = 0; // 1 行入力の読み取り位置

void hal_display_set_mock_input(const char* input) {
    strncpy(mock_input_buf, input, sizeof(mock_input_buf) - 1);
    mock_input_buf[sizeof(mock_input_buf) - 1] = '\0';
    mock_line_pos = 0;
    extern int mock_get_key_pos;
    mock_get_key_pos = 0; // GET 用の読み取り位置も先頭へ
}

void hal_system_wait(int ms) {
    if (ms > 0) usleep(ms * 1000);
}

// スクロール範囲（CONSOLE 用）。テストからは記録だけ確認できる
static int mock_scroll_top = 0;
static int mock_scroll_bottom = 29; // 320x240 / 8 = 30 行

void hal_display_set_scroll_region(int top_row, int bottom_row) {
    mock_scroll_top = top_row;
    mock_scroll_bottom = bottom_row;
}

int hal_display_text_rows() { return 30; }
int hal_display_text_cols() { return 40; }

namespace mock_hal {
    int get_scroll_top()    { return mock_scroll_top; }
    int get_scroll_bottom() { return mock_scroll_bottom; }
}

// テストの入力は 1 バイトずつここから流す。こうすると INPUT / LINE INPUT が
// 本物の行エディタ（src/line_input.cpp）を通るので、多バイト文字の畳み込みや
// バックスペースの扱いまでホストで検証できる。
//
// 尽きたら改行を返して行を確定させ、位置を先頭へ戻す。1 回の
// hal_display_set_mock_input() で何度 INPUT しても同じ内容が読める
// （差し込んだ値をそのまま返していた従来のモックと同じ挙動）。
int hal_system_getchar_timeout(int) {
    if (mock_input_buf[mock_line_pos] == '\0') {
        mock_line_pos = 0;
        return '\n';
    }
    return (unsigned char)mock_input_buf[mock_line_pos++];
}

int hal_system_break_requested() {
    // ホストテストでは中断しない（無限ループのテストは max_steps で止める）
    return 0;
}

// GET が返すキーを 1 文字ずつ供給する。set_mock_input で仕込んだ文字列を先頭から消費する
int mock_get_key_pos = 0;

int hal_system_get_key() {
    if (mock_input_buf[mock_get_key_pos] == '\0') return 0;
    return (unsigned char)mock_input_buf[mock_get_key_pos++];
}

// ---------------------------------------------------------
// Touch HAL mock — injectable state for unit tests
// ---------------------------------------------------------
static int mock_touch_state = 0;
static int mock_touch_x     = 0;
static int mock_touch_y     = 0;

void hal_touch_init() {
    mock_touch_state = 0;
    mock_touch_x     = 0;
    mock_touch_y     = 0;
}

int hal_touch_is_touched() {
    return mock_touch_state;
}

int hal_touch_get_x() {
    return mock_touch_x;
}

int hal_touch_get_y() {
    return mock_touch_y;
}

namespace mock_hal {
void set_touch_state(int touched, int x, int y) {
    mock_touch_state = touched;
    mock_touch_x     = x;
    mock_touch_y     = y;
}
} // namespace mock_hal (touch injection)


// 起動（正確には最初の呼び出し）からの経過ミリ秒。TIMER 関数用
#include <chrono>
uint32_t hal_system_millis() {
    static const auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
}

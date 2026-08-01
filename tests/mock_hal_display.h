#pragma once
#include <string>
#include <vector>

// ---------------------------------------------------------
// Host-side Mock Interface for Testing
// ---------------------------------------------------------

struct DrawCommand {
    enum Type { PSET, LINE, CIRCLE };
    Type type;
    int x1, y1, x2, y2, r;
    uint16_t color;
};

namespace mock_hal {

struct GpioCommand {
    int pin, mode, pull;
    bool value;
};

// Fetch all text printed via hal_display_print
std::vector<std::string> get_printed_lines();

// Fetch the raw concatenated string printed
std::string get_raw_print_buffer();

// Fetch recorded draw commands
std::vector<DrawCommand> get_draw_commands();

// Fetch recorded GPIO commands
std::vector<GpioCommand> get_gpio_commands();

// 転送の遅延（SYNC）の確認用
int  get_flush_count();
void reset_flush_count();

// Fetch recorded brightness levels
std::vector<int> get_brightness_levels();

// Reset the internal state for the next test
void reset();

// Check if clear screen was called
bool was_cls_called();

// Inject touch state for TOUCH() function tests
void set_touch_state(int touched, int x, int y);

// CONSOLE で設定されたスクロール範囲（行）を確認する
int get_scroll_top();
int get_scroll_bottom();

// LOCATE / CONSOLE で移動したカーソル位置を確認する
int get_cursor_x();
int get_cursor_y();

} // namespace mock_hal

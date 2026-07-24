#include "hal_display.h"
#include <stdio.h>
#include <stdlib.h>

#if __has_include("pico/stdlib.h")
#include "hal_font.h"
#include "screenshot.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include <string.h>

// Waveshare RP2350-Touch-LCD-2.8 Pinout
#define LCD_MOSI 11
#define LCD_MISO 12
#define LCD_SCK 10
#define LCD_CS 13
#define LCD_DC 14
#define LCD_RST 15
#define LCD_BL 16

#define SPI_PORT spi1

#define LCD_WIDTH 320
#define LCD_HEIGHT 240
static uint16_t frame_buffer[LCD_WIDTH * LCD_HEIGHT];

#define FONT_WIDTH 8
#define FONT_HEIGHT 8
#define TEXT_COLS (LCD_WIDTH / FONT_WIDTH)
#define TEXT_ROWS (LCD_HEIGHT / FONT_HEIGHT)

static int cursor_x = 0;
static int cursor_y = 0;

extern uint16_t current_color_565;

static void lcd_write_cmd(uint8_t cmd) {
  gpio_put(LCD_DC, 0);
  gpio_put(LCD_CS, 0);
  spi_write_blocking(SPI_PORT, &cmd, 1);
  gpio_put(LCD_CS, 1);
}

static void lcd_write_data(uint8_t data) {
  gpio_put(LCD_DC, 1);
  gpio_put(LCD_CS, 0);
  spi_write_blocking(SPI_PORT, &data, 1);
  gpio_put(LCD_CS, 1);
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  lcd_write_cmd(0x2A); // Column Address Set
  lcd_write_data(x0 >> 8);
  lcd_write_data(x0 & 0xFF);
  lcd_write_data(x1 >> 8);
  lcd_write_data(x1 & 0xFF);

  lcd_write_cmd(0x2B); // Row Address Set
  lcd_write_data(y0 >> 8);
  lcd_write_data(y0 & 0xFF);
  lcd_write_data(y1 >> 8);
  lcd_write_data(y1 & 0xFF);

  lcd_write_cmd(0x2C); // Memory Write
}

void hal_display_cls() {
  memset(frame_buffer, 0, sizeof(frame_buffer)); // 黒でクリア
  cursor_x = 0;
  cursor_y = 0;
  hal_display_sync();
}

void hal_display_init() {
  // 1. SPI通信の安定化 (安定確実な30MHzに落とし、フォーマットを明示)
  spi_init(SPI_PORT, 30 * 1000 * 1000);
  spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

  gpio_set_function(LCD_SCK, GPIO_FUNC_SPI);
  gpio_set_function(LCD_MOSI, GPIO_FUNC_SPI);

  // 2. GPIO初期化と「待機状態(1)」の固定
  gpio_init(LCD_DC);
  gpio_set_dir(LCD_DC, GPIO_OUT);
  gpio_put(LCD_DC, 1);

  gpio_init(LCD_CS);
  gpio_set_dir(LCD_CS, GPIO_OUT);
  gpio_put(LCD_CS, 1); // ★超重要: 初期状態は必ず1(非選択)

  gpio_init(LCD_RST);
  gpio_set_dir(LCD_RST, GPIO_OUT);
  gpio_put(LCD_RST, 1);

  // バックライトPWM
  gpio_set_function(LCD_BL, GPIO_FUNC_PWM);
  uint slice_num = pwm_gpio_to_slice_num(LCD_BL);
  pwm_set_wrap(slice_num, 100);
  pwm_set_enabled(slice_num, true);
  hal_display_set_brightness(80);

  // ハードウェアリセット
  gpio_put(LCD_RST, 1);
  sleep_ms(100);
  gpio_put(LCD_RST, 0);
  sleep_ms(100);
  gpio_put(LCD_RST, 1);
  sleep_ms(120);

  // 3. ST7789 初期化コマンド
  lcd_write_cmd(0x01); // SW Reset
  sleep_ms(150);
  lcd_write_cmd(0x11); // Sleep Out
  sleep_ms(120);       // ★データシート指定の必須ウェイト

  lcd_write_cmd(0x3A);
  lcd_write_data(0x05); // 16-bit color
  lcd_write_cmd(0x36);
  lcd_write_data(0x70); // Landscape
  lcd_write_cmd(0x21);  // Display Inversion On
  lcd_write_cmd(0x13);  // Normal Display Mode On

  lcd_write_cmd(0x29); // Display On
  sleep_ms(100);

  // === スプラッシュ画面 ===
  hal_display_cls(); // 背景を青色にする

  int cx = LCD_WIDTH / 2;
  int cy = LCD_HEIGHT / 2;
  for (int r = 10; r <= 80; r += 10) {
    hal_graphics_circle(cx, cy, r, 0x07E0); // 緑の円
  }

  cursor_x = 9;
  cursor_y = 12;
  current_color_565 = 0xFFFF; // テキスト色を白に強制
  hal_display_print("pico-basic Booting...");

  hal_display_sync();

  sleep_ms(2000); // スプラッシュ画面を2秒表示

  // 5秒後に青色背景にクリアして REPL(Ready) 待機状態へ
  hal_display_cls();
}
void hal_display_set_brightness(int level) {
  if (level < 0)
    level = 0;
  if (level > 100)
    level = 100;
  pwm_set_gpio_level(LCD_BL, level);
}

void hal_graphics_pset(int x, int y, uint16_t color) {
  if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT)
    return;
  frame_buffer[y * LCD_WIDTH + x] = color;
}

uint16_t hal_graphics_get_pixel(int x, int y) {
  if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT)
    return 0;
  return frame_buffer[y * LCD_WIDTH + x];
}

void hal_display_sync() {
  lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
  gpio_put(LCD_DC, 1);
  gpio_put(LCD_CS, 0);
  // Push the whole frame buffer via SPI
  spi_write_blocking(SPI_PORT, (uint8_t *)frame_buffer,
                     LCD_WIDTH * LCD_HEIGHT * 2);
  gpio_put(LCD_CS, 1);
}

// 更新した矩形だけを転送する。
// 全画面転送は 153,600 バイト（30MHz で約 41ms）かかり、1文字ごとに呼ぶと
// その間 CPU が止まって USB からの入力を取りこぼす。文字セル単位なら 128 バイト。
void hal_display_sync_rect(int x0, int y0, int w, int h) {
  if (w <= 0 || h <= 0)
    return;
  if (x0 < 0) { w += x0; x0 = 0; }
  if (y0 < 0) { h += y0; y0 = 0; }
  if (x0 + w > LCD_WIDTH)  w = LCD_WIDTH - x0;
  if (y0 + h > LCD_HEIGHT) h = LCD_HEIGHT - y0;
  if (w <= 0 || h <= 0)
    return;

  lcd_set_window(x0, y0, x0 + w - 1, y0 + h - 1);
  gpio_put(LCD_DC, 1);
  gpio_put(LCD_CS, 0);
  // フレームバッファは全幅で連続しているため、行ごとに切り出して送る
  for (int row = 0; row < h; row++) {
    spi_write_blocking(SPI_PORT,
                       (uint8_t *)(frame_buffer + (y0 + row) * LCD_WIDTH + x0),
                       w * 2);
  }
  gpio_put(LCD_CS, 1);
}

void hal_graphics_line(int x1, int y1, int x2, int y2, uint16_t color) {
  // Bresenham's algorithm
  int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
  int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
  int err = dx + dy, e2;
  while (true) {
    hal_graphics_pset(x1, y1, color);
    if (x1 == x2 && y1 == y2)
      break;
    e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x1 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y1 += sy;
    }
  }
}

void hal_graphics_circle(int x0, int y0, int radius, uint16_t color) {
  int x = radius, y = 0;
  int err = 0;
  while (x >= y) {
    hal_graphics_pset(x0 + x, y0 + y, color);
    hal_graphics_pset(x0 + y, y0 + x, color);
    hal_graphics_pset(x0 - y, y0 + x, color);
    hal_graphics_pset(x0 - x, y0 + y, color);
    hal_graphics_pset(x0 - x, y0 - y, color);
    hal_graphics_pset(x0 - y, y0 - x, color);
    hal_graphics_pset(x0 + y, y0 - x, color);
    hal_graphics_pset(x0 + x, y0 - y, color);
    if (err <= 0) {
      y += 1;
      err += 2 * y + 1;
    }
    if (err > 0) {
      x -= 1;
      err -= 2 * x + 1;
    }
  }
}

static void char_at(int x, int y, char c, uint16_t fg, uint16_t bg) {
  // バイトを 0-255 として扱う。0xA1-0xDF は JIS X 0201 半角カタカナ
  unsigned idx = (unsigned char)c;
  for (int j = 0; j < 8; j++) {
    uint8_t row = BASIC_FONT[idx][j];
    for (int i = 0; i < 8; i++) {
      uint16_t color = (row & (0x80 >> i)) ? fg : bg;
      hal_graphics_pset(x + i, y + j, color);
    }
  }
}

// CONSOLE でスクロール範囲を制限できる。既定は全画面（0 .. TEXT_ROWS-1）
static int scroll_top_row = 0;
static int scroll_bottom_row = TEXT_ROWS - 1;

void hal_display_set_scroll_region(int top_row, int bottom_row) {
  if (top_row < 0) top_row = 0;
  if (bottom_row > TEXT_ROWS - 1) bottom_row = TEXT_ROWS - 1;
  if (top_row > bottom_row) return; // 不正な指定は無視

  scroll_top_row = top_row;
  scroll_bottom_row = bottom_row;
}

int hal_display_text_rows() { return TEXT_ROWS; }
int hal_display_text_cols() { return TEXT_COLS; }

// スクロール領域内だけを 1 行分持ち上げ、最下行をクリアする
static void scroll_up() {
  int top_px    = scroll_top_row * FONT_HEIGHT;
  int region_rows = scroll_bottom_row - scroll_top_row + 1;
  int region_px = region_rows * FONT_HEIGHT;

  uint16_t* base = frame_buffer + top_px * LCD_WIDTH;
  memmove(base, base + FONT_HEIGHT * LCD_WIDTH,
          (region_px - FONT_HEIGHT) * LCD_WIDTH * 2);
  memset(base + (region_px - FONT_HEIGHT) * LCD_WIDTH, 0,
         FONT_HEIGHT * LCD_WIDTH * 2);
}

void hal_display_print(const char *text) {
  // 描き換えた範囲だけを転送するため、この呼び出しでの更新領域を追う
  int dirty_x0 = LCD_WIDTH, dirty_y0 = LCD_HEIGHT;
  int dirty_x1 = -1, dirty_y1 = -1;
  bool needs_full_sync = false;

  while (*text) {
    char c = *text++;
    if (c == '\n') {
      cursor_x = 0;
      cursor_y++;
    } else if (c == '\r') {
      cursor_x = 0;
    } else if (c == '\t') {
      cursor_x = (cursor_x / 8 + 1) * 8;
    } else if (c == '\b') {
      if (cursor_x > 0) {
        cursor_x--;
      } else if (cursor_y > 0) {
        cursor_y--;
        cursor_x = TEXT_COLS - 1;
      }
    } else {
      int px = cursor_x * FONT_WIDTH;
      int py = cursor_y * FONT_HEIGHT;
      char_at(px, py, c, current_color_565, 0x0000);
      if (px < dirty_x0) dirty_x0 = px;
      if (py < dirty_y0) dirty_y0 = py;
      if (px + FONT_WIDTH  > dirty_x1) dirty_x1 = px + FONT_WIDTH;
      if (py + FONT_HEIGHT > dirty_y1) dirty_y1 = py + FONT_HEIGHT;
      cursor_x++;
    }

    if (cursor_x >= TEXT_COLS) {
      cursor_x = 0;
      cursor_y++;
    }
    if (cursor_y > scroll_bottom_row) {
      scroll_up();
      cursor_y = scroll_bottom_row;
      needs_full_sync = true; // 画面全体がずれるので部分転送では足りない
    }
  }

  if (needs_full_sync) {
    hal_display_sync();
  } else if (dirty_x1 > 0) {
    hal_display_sync_rect(dirty_x0, dirty_y0, dirty_x1 - dirty_x0, dirty_y1 - dirty_y0);
  }
}

void hal_display_locate(int x, int y) {
  if (x >= 0 && x < TEXT_COLS)
    cursor_x = x;
  if (y >= 0 && y < TEXT_ROWS)
    cursor_y = y;
}

void hal_display_get_info(int &width, int &height) {
  width = LCD_WIDTH;
  height = LCD_HEIGHT;
}

void hal_display_input(char *buffer, int max_len) {
  int input_ptr = 0;
  memset(buffer, 0, max_len);

  while (true) {
    int c = getchar();

    if (c == EOF)
      continue;

    if (c == '\r' || c == '\n') {
      printf("\n");
      hal_display_print("\n");
      break;
    } else if (c == '\b' || c == 127) {
      if (input_ptr > 0) {
        input_ptr--;
        buffer[input_ptr] = '\0';
        printf("\b \b");
        hal_display_print("\b \b");
      }
    } else if ((c >= 32 && c <= 126) || (c >= 0xA1 && c <= 0xDF)) {
      // 印字可能 ASCII と半角カタカナ（0xA1-0xDF）
      if (input_ptr < max_len - 1) {
        buffer[input_ptr++] = static_cast<char>(c);
        buffer[input_ptr] = '\0';
        putchar(c);

        char s[2] = {static_cast<char>(c), '\0'};
        hal_display_print(s);
      }
    }
  }
}

void hal_system_wait(int ms) {
  if (ms > 0)
    sleep_ms(ms);
}

uint32_t hal_system_millis() {
  return to_ms_since_boot(get_absolute_time());
}

int hal_system_break_requested() {
  // 待たずに 1 文字だけ覗く。Ctrl-C(0x03) なら中断、それ以外は捨てる
  int c = getchar_timeout_us(0);
  if (c == 0x10) { // Ctrl-P: 実行中でも画面を BMP で保存できる
    char shot[16];
    // 通知はシリアルだけ。LCD に書くと撮った直後の画面が崩れる
    if (screenshot_save_next(shot, sizeof(shot)))
      printf("\n[screen saved: %s]\n", shot);
    else
      printf("\n[screen save failed]\n");
    return 0; // 中断ではない
  }
  return (c == 0x03) ? 1 : 0;
}

int hal_system_get_key() {
  int c = getchar_timeout_us(0);
  if (c == PICO_ERROR_TIMEOUT || c < 0) return 0; // 押されていない
  if (c == 0x03) return 0;                          // Ctrl-C は中断に任せる
  return c;
}
#else
// ---------------------------------------------------------
// Mock implementations for host build (linking to mock_hal)
// ---------------------------------------------------------

void hal_display_init() {}

void hal_display_print(const char *text) {
  // Implementation is in mock_hal_display.cpp
}

void hal_display_cls() {
  // Implementation is in mock_hal_display.cpp
}

void hal_display_locate(int x, int y) {}

void hal_graphics_pset(int x, int y, uint16_t color) {
  // Implementation is in mock_hal_display.cpp
}

void hal_graphics_line(int x1, int y1, int x2, int y2, uint16_t color) {
  // Implementation is in mock_hal_display.cpp
}

void hal_graphics_circle(int x, int y, int r, uint16_t color) {
  // Implementation is in mock_hal_display.cpp
}

void hal_display_get_info(int &width, int &height) {
  width = 320;
  height = 240;
}

void hal_display_set_brightness(int level) {}

#endif

#pragma once
#include <stdint.h>

// pico-basic HAL display
// Hardware Abstraction Layer for Display targeting Waveshare RP2350-Touch-LCD-2.8

// Phase 2: SPI Display Driver Implementation
void hal_display_init();

// Text Output related functions
void hal_display_print(const char* text);
void hal_display_cls();
void hal_display_locate(int x, int y);

// Graphics Output related functions
void hal_graphics_pset(int x, int y, uint16_t color);
void hal_graphics_line(int x1, int y1, int x2, int y2, uint16_t color);
void hal_graphics_circle(int x, int y, int r, uint16_t color);

// Info helper
void hal_display_get_info(int& width, int& height);

// Backlight control
void hal_display_set_brightness(int level);

// Frame buffer operations
uint16_t hal_graphics_get_pixel(int x, int y);
void hal_display_sync();

// 指定した矩形だけを LCD に転送する。
// 全画面転送は 153,600 バイト（約 41ms）かかるため、描画のたびに呼ぶ用途では
// 更新した範囲だけを渡すこと。画面外にはみ出す指定は内部で切り詰める。
void hal_display_sync_rect(int x, int y, int w, int h);

// Text Input related functions
void hal_display_input(char* buffer, int max_len);

// System functions
void hal_system_wait(int ms);

// 実行中に中断（Ctrl-C）が要求されたかを調べる。
// ブロックせずに戻ること。要求されていれば 1、されていなければ 0。
int hal_system_break_requested();

void hal_display_set_mock_input(const char* input);

#pragma once

#include <stdint.h>

/// @file hal_display.h
/// LCD への文字・図形の描画と、端末まわりのシステム関数。
///
/// Hardware Abstraction Layer for Display targeting Waveshare RP2350-Touch-LCD-2.8

/**
 * @brief LCD と PWM バックライトを初期化し、スプラッシュを表示する
 *
 * Phase 2: SPI Display Driver Implementation
 */
void hal_display_init();

/**
 * @brief 文字列をカーソル位置へ描く。
 *
 * Text Output related functions
 *
 * @param text 描く文字列（\n / \b を解釈する）
 */
void hal_display_print(const char* text);
/// @brief 画面を消してカーソルを左上へ戻す
void hal_display_cls();
/**
 * @brief カーソル位置を移す。
 * @param x 桁（0 起点）
 * @param y 行（0 起点）
 */
void hal_display_locate(int x, int y);

/**
 * @brief 点を打つ。
 *
 * Graphics Output related functions
 *
 * @param x 画面の X 座標（範囲外は無視）
 * @param y 画面の Y 座標（範囲外は無視）
 * @param color RGB565
 */
void hal_graphics_pset(int x, int y, uint16_t color);
/**
 * @brief 線を引く。
 * @param x1,y1 始点
 * @param x2,y2 終点
 * @param color RGB565
 */
void hal_graphics_line(int x1, int y1, int x2, int y2, uint16_t color);
/**
 * @brief 円を描く。
 * @param x,y 中心
 * @param r 半径
 * @param color RGB565
 */
void hal_graphics_circle(int x, int y, int r, uint16_t color);

/**
 * @brief 画面の大きさを返す。
 *
 * Info helper
 *
 * @param[out] width 横の画素数
 * @param[out] height 縦の画素数
 */
void hal_display_get_info(int& width, int& height);

/**
 * @brief バックライトの明るさを変える。
 *
 * Backlight control
 *
 * @param level 0-100（%）
 */
void hal_display_set_brightness(int level);

/**
 * @brief 画素の色を読む。
 *
 * Frame buffer operations
 *
 * @param x,y 画面座標
 * @return RGB565。範囲外なら 0
 */
uint16_t hal_graphics_get_pixel(int x, int y);
/**
 * @brief テキストのスクロール範囲を絞る（CONSOLE）。
 *
 * top_row 以上 bottom_row 以下（両端含む）だけがスクロール対象になる。
 * 全画面に戻すには 0 と（行数-1）を渡す。
 *
 * @param top_row 上端の行
 * @param bottom_row 下端の行
 */
void hal_display_set_scroll_region(int top_row, int bottom_row);

/**
 * @brief テキストの行数（WIDTH / CONSOLE の範囲検査に使う）。
 *
 * @return 行数
 */
int hal_display_text_rows();
/**
 * @brief テキストの桁数。
 * @return 桁数
 */
int hal_display_text_cols();

/// @brief フレームバッファ全体を LCD へ転送する
void hal_display_sync();

/**
 * @brief 指定した矩形だけ転送する。
 *
 * 全画面転送は 153,600 バイト（約 41ms）かかるため、描画のたびに呼ぶ用途では
 * 更新した範囲だけを渡すこと。画面外にはみ出す指定は内部で切り詰める。
 *
 * @param x,y 矩形の左上
 * @param w,h 矩形の大きさ
 */
void hal_display_sync_rect(int x, int y, int w, int h);

/**
 * @brief 転送の遅延を切り替える（BASIC の SYNC）。
 *
 * 既定では描画のたびに LCD へ転送するため、「消してから描き直す」書き方だと
 * 消えた状態が一瞬画面に出てちらつく。遅延させると更新範囲を覚えるだけになり、
 * hal_display_flush() でまとめて転送できる。
 *
 * 遅延中は文字の表示も出ないので、プログラム終了時と Ready のたびに
 * 呼び出し側で必ず解除すること（画面が固まったように見えるため）。
 *
 * @param deferred true でためる、false でためた分を出して通常動作へ
 */
void hal_display_set_deferred(bool deferred);
/**
 * @brief 転送が遅延中か。
 * @return 遅延中なら true
 */
bool hal_display_is_deferred();

/**
 * @brief ためた更新範囲をまとめて転送する
 */
void hal_display_flush();

/**
 * @brief 指定した時間だけ待つ。
 *
 * System functions
 *
 * @param ms ミリ秒。0 以下なら何もしない
 */
void hal_system_wait(int ms);

/**
 * @brief 起動からの経過時間。
 * @return ミリ秒
 */
uint32_t hal_system_millis();

/**
 * @brief Ctrl-C が押されているか、待たずに調べる。
 * ブロックせずに戻ること。要求されていれば 1、されていなければ 0。
 *
 * @return 要求されていれば 1
 */
int hal_system_break_requested();

/**
 * @brief タイムアウト付きの 1 文字入力。
 *
 * 入力待ちで塞がっている間も電源ボタンを見られるよう、文字入力は必ず
 * これを経由する（src/line_input.cpp がまとめて面倒を見る）
 *
 * @param timeout_us 待つ時間（マイクロ秒）
 * @return 文字コード。時間切れなら -1
 */
int hal_system_getchar_timeout(int timeout_us);

/**
 * @brief 待たずに 1 文字取得する（BASIC の GET 用）。
 *
 * 押されていなければ 0 を返す。Ctrl-C は中断扱いで捨てる。
 *
 * @return 文字コード。押されていなければ 0
 */
int hal_system_get_key();

/**
 * @brief テスト用の差し込み口（実機では何もしない）。
 * @param input 以降の入力として 1 バイトずつ読み出される文字列
 */
void hal_display_set_mock_input(const char* input);

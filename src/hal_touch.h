#pragma once
#include <stdint.h>

/// @file hal_touch.h
/// タッチパネル。BASIC の TOUCH(n) 関数がそのまま対応する。
///   TOUCH(0) -> X 座標 / TOUCH(1) -> Y 座標 / TOUCH(2) -> 触れているか

#ifdef __cplusplus
extern "C" {
#endif

/// @brief タッチコントローラを初期化する。見つからなければ以降 0 を返し続ける
void hal_touch_init();

/**
 * @brief いま触れられているか。
 * @return 触れていれば 1、そうでなければ 0（コントローラが無い場合も 0）
 */
int hal_touch_is_touched();

/**
 * @brief タッチ位置の X 座標。
 * @return 0-319。hal_touch_is_touched() が 1 のときだけ意味を持つ
 */
int hal_touch_get_x();

/**
 * @brief タッチ位置の Y 座標。
 * @return 0-239。hal_touch_is_touched() が 1 のときだけ意味を持つ
 */
int hal_touch_get_y();

#ifdef __cplusplus
}
#endif

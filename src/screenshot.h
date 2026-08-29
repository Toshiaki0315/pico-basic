#pragma once

/// @file screenshot.h
/// 画面（フレームバッファ）を BMP で SD カードに保存する。
///
/// PNG ではなく BMP なのは、PNG が deflate 圧縮と CRC を要するのに対し
/// BMP はヘッダ + 生画素だけで書けるため。320x240 の 24bit BMP は約 225KB で、
/// 非圧縮 PNG にしたときとほぼ同じ大きさになる。
///
/// 実装はハードウェアに依存しない（hal_graphics_get_pixel と hal_file_* だけを使う）。

/**
 * @brief 指定したファイル名で画面を保存する。
 * @param path 保存先のパス（SD カード上）
 * @return 書き込めたら true。カードが無い・書き込めない場合は false
 */
bool screenshot_save(const char* path);

/**
 * @brief SCR00.BMP … SCR99.BMP の空き番号を探して保存する。
 * @param[out] out_name 実際に使ったファイル名が入る
 * @param out_size out_name の大きさ（16 バイト以上）
 * @return 保存できたら true。空き番号が無い場合も false
 */
bool screenshot_save_next(char* out_name, int out_size);

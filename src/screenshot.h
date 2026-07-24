#pragma once

// 画面（フレームバッファ）を BMP で SD カードに保存する。
//
// PNG ではなく BMP なのは、PNG が deflate 圧縮と CRC を要するのに対し
// BMP はヘッダ + 生画素だけで書けるため。320x240 の 24bit BMP は約 225KB で、
// 非圧縮 PNG にしたときとほぼ同じ大きさになる。
//
// 実装はハードウェアに依存しない（hal_graphics_get_pixel と hal_file_* だけを使う）。

// 指定したファイル名で保存する。成功したら true。
bool screenshot_save(const char* path);

// SCR00.BMP … SCR99.BMP の空き番号を探して保存する。
// 実際に使ったファイル名を out_name に返す（out_name は 16 バイト以上）。
bool screenshot_save_next(char* out_name, int out_size);

#pragma once
#include <cstring>

// ---------------------------------------------------------
// 固定長バッファへの文字列コピー
//
// strncpy は「入りきったときだけ終端する」ので、正しく使うには毎回もう 1 行
// 要る。その 1 行は書き忘れても普段は動いてしまう（このプロジェクトでは
// 識別子が 8 文字以下という字句解析側の都合に救われていた）ため、
// 意図を名前にして 1 か所にまとめる。
//
// 生メモリから「終端があるとは限らない範囲」を読むときは、ここではなく
// strncpy に上限を渡すこと。下の関数は src が終端済みであることを前提にする。
// ---------------------------------------------------------

// 収まらなければ切り詰めて、必ず終端する。src は終端済みの C 文字列であること。
//
// strlen を使うのは、strnlen で上限を切ると src が dst より小さい配列だった
// ときに「上限まで読むかもしれない」と警告されるため。
inline void copy_string(char* dst, size_t dst_size, const char* src) {
    if (dst_size == 0) return;
    size_t n = strlen(src);
    if (n > dst_size - 1) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

// 終端を持たない固定長の欄に書く（論理メモリ上の 8 バイトの変数名など）。
// 短い名前は 0 で埋める。strncpy の埋める挙動をそのまま書き下したもの。
inline void copy_fixed_field(void* dst, size_t width, const char* src) {
    size_t n = strlen(src);
    if (n > width) n = width;
    memset(dst, 0, width);
    memcpy(dst, src, n);
}

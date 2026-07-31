#pragma once
#include <stdio.h>

// ---------------------------------------------------------
// 半角カタカナのシリアル出力
//
// LCD は JIS X 0201 の 1 バイトコード（0xA1-0xDF）でフォントを引くが、
// そのバイトをそのままシリアルへ流すと UTF-8 として不正なので、
// 端末では「」に化けてしまう。
//
// JIS X 0201 の半角カタカナは Unicode の U+FF61-U+FF9F に順番どおり
// 対応しているので、差分を足すだけで変換できる。
//   0xA1 (｡) -> U+FF61   …   0xDF (ﾟ) -> U+FF9F
//
// 変換するのはシリアルへ書くときだけ。LCD 側は生のバイトのままなので、
// フォントや LEN / MID$ の 1 バイト単位の扱いには影響しない。
// ---------------------------------------------------------

// 戻り値は out に書いたバイト数（半角カタカナなら 3、それ以外は 0）。
inline int jis_kana_to_utf8(unsigned char c, char out[3]) {
    if (c < 0xA1 || c > 0xDF) return 0;
    unsigned int cp = 0xFF61u + (unsigned int)(c - 0xA1u);
    out[0] = (char)(0xE0u | (cp >> 12));
    out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[2] = (char)(0x80u | (cp & 0x3Fu));
    return 3;
}

// シリアル(stdout)へ 1 バイト書く。半角カタカナだけ UTF-8 にする。
inline void serial_putc_kana(unsigned char c) {
    char utf8[3];
    int n = jis_kana_to_utf8(c, utf8);
    if (n > 0) fwrite(utf8, 1, (size_t)n, stdout);
    else putchar(c);
}

inline void serial_print_kana(const char* s) {
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        serial_putc_kana(*p);
    }
}

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
//
// 出力は必ず putchar で行うこと。Pico SDK が USB CDC / UART に繋いでいるのは
// printf / puts / putchar / getchar だけ（pico_stdio の pico_wrap_function）で、
// fwrite は newlib の FILE 経由になり端末には何も届かない。
inline void serial_putc_kana(unsigned char c) {
    char utf8[3];
    int n = jis_kana_to_utf8(c, utf8);
    if (n > 0) {
        for (int i = 0; i < n; i++) putchar((unsigned char)utf8[i]);
    } else {
        putchar(c);
    }
}

// 入力側の逆変換。UTF-8 の 3 バイトが半角カタカナ（U+FF61-U+FF9F）なら
// JIS X 0201 の 1 バイト（0xA1-0xDF）を返す。該当しなければ 0。
//
// 端末は 3 バイトで送ってくるので、2 バイトだけ読み捨てると 1 バイト分ずれて
// 残りが別の文字に化ける（`ﾀﾁﾂ` が `ｾ` になる、など）。必ず 3 バイトで扱うこと。
inline unsigned char utf8_to_jis_kana(unsigned char b1, unsigned char b2, unsigned char b3) {
    if ((b1 & 0xF0) != 0xE0) return 0;                     // 3 バイト文字の 1 バイト目か
    if ((b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return 0; // 継続バイトか
    unsigned int cp = ((unsigned int)(b1 & 0x0F) << 12) |
                      ((unsigned int)(b2 & 0x3F) << 6) |
                      ((unsigned int)(b3 & 0x3F));
    if (cp < 0xFF61 || cp > 0xFF9F) return 0;              // 半角カタカナ以外
    return (unsigned char)(0xA1 + (cp - 0xFF61));
}

inline void serial_print_kana(const char* s) {
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        serial_putc_kana(*p);
    }
}

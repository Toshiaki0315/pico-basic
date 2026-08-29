#include "screenshot.h"
#include "strutil.h"
#include "hal_display.h"
#include "hal_sdcard.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// BMP は行を下から上へ、画素を B,G,R の順で並べる。
// 320 px * 3 byte = 960 は 4 の倍数なので行末パディングは不要だが、
// 幅が変わっても壊れないようパディングは計算して書き出す。
#define BMP_FILE_HEADER 14
#define BMP_INFO_HEADER 40

static void put_u16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}
static void put_u32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

// RGB565 → RGB888。上位ビットを下位に複製して、白が 0xFF になるようにする
static inline void rgb565_to_888(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b) {
    uint8_t r5 = (uint8_t)((c >> 11) & 0x1F);
    uint8_t g6 = (uint8_t)((c >> 5)  & 0x3F);
    uint8_t b5 = (uint8_t)( c        & 0x1F);
    r = (uint8_t)((r5 << 3) | (r5 >> 2));
    g = (uint8_t)((g6 << 2) | (g6 >> 4));
    b = (uint8_t)((b5 << 3) | (b5 >> 2));
}

bool screenshot_save(const char* path) {
    int width = 0, height = 0;
    hal_display_get_info(width, height);
    if (width <= 0 || height <= 0 || width > 1024) return false;

    const int row_bytes = width * 3;
    const int padding   = (4 - (row_bytes % 4)) % 4;
    const uint32_t image_size = (uint32_t)(row_bytes + padding) * (uint32_t)height;
    const uint32_t file_size  = BMP_FILE_HEADER + BMP_INFO_HEADER + image_size;

    void* fp = hal_file_open(path, "wb");
    if (!fp) return false;

    uint8_t header[BMP_FILE_HEADER + BMP_INFO_HEADER];
    memset(header, 0, sizeof(header));
    header[0] = 'B'; header[1] = 'M';
    put_u32(&header[2], file_size);
    put_u32(&header[10], BMP_FILE_HEADER + BMP_INFO_HEADER); // 画素データの位置
    put_u32(&header[14], BMP_INFO_HEADER);                   // BITMAPINFOHEADER
    put_u32(&header[18], (uint32_t)width);
    put_u32(&header[22], (uint32_t)height);                  // 正 = 下から上
    put_u16(&header[26], 1);                                 // プレーン数
    put_u16(&header[28], 24);                                // 24bit
    put_u32(&header[34], image_size);
    put_u32(&header[38], 2835);                              // 72dpi（横）
    put_u32(&header[42], 2835);                              // 72dpi（縦）

    if (hal_file_write(header, 1, sizeof(header), fp) != sizeof(header)) {
        hal_file_close(fp);
        return false;
    }

    // 1 行ずつ組み立てて書く。画素ごとに書くと SD が遅すぎる
    static uint8_t row[1024 * 3 + 4];
    for (int y = height - 1; y >= 0; y--) { // BMP は最下行から
        int o = 0;
        for (int x = 0; x < width; x++) {
            uint8_t r, g, b;
            rgb565_to_888(hal_graphics_get_pixel(x, y), r, g, b);
            row[o++] = b; // BMP は BGR 順
            row[o++] = g;
            row[o++] = r;
        }
        for (int p = 0; p < padding; p++) row[o++] = 0;
        if (hal_file_write(row, 1, (size_t)o, fp) != (size_t)o) {
            hal_file_close(fp);
            return false;
        }
    }
    hal_file_close(fp);
    return true;
}

bool screenshot_save_next(char* out_name, int out_size) {
    for (int n = 0; n < 100; n++) {
        char name[16];
        snprintf(name, sizeof(name), "SCR%02d.BMP", n);
        void* probe = hal_file_open(name, "rb");
        if (probe) {           // 既にある → 次の番号へ
            hal_file_close(probe);
            continue;
        }
        if (!screenshot_save(name)) return false;
        if (out_name && out_size > 0) {
            copy_string(out_name, (size_t)out_size, name);
        }
        return true;
    }
    return false; // SCR00〜SCR99 が全部埋まっている
}

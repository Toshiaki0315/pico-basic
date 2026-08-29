#pragma once

/// @file psg_envelope.h
/// PSG（AY-3-8910 相当）のハードウェアエンベロープ。
/// ハードウェアに依存しないので、ホストのテストからそのまま検証できる。
#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------
// AY-3-8910 エンベロープの純粋ロジック（Pico / ホスト共通）
//
// 状態を引数で受け取るので単体テストできる。
//   vol  … 現在の振幅 0〜15
//   dir  … +1 上昇 / -1 下降
//   hold … 端で保持中
// shape は R13 の下位 4bit（bit3 Continue / bit2 Attack / bit1 Alternate / bit0 Hold）。
// ---------------------------------------------------------

// 形状書き込み時の初期化
/**
 * @brief 形状に応じてエンベロープの初期状態を作る。
 * @param[out] vol 音量（0-15）
 * @param[out] dir 進む向き（+1 で増加、-1 で減少）
 * @param[out] hold 端に達したら止まるか
 * @param shape 形状レジスタの値（下位 4bit）
 */
static inline void psg_envelope_init(int* vol, int* dir, bool* hold, uint8_t shape) {
    bool attack = shape & 0x04;
    *vol  = attack ? 0 : 15;   // Attack:0 から上昇 / それ以外:15 から下降
    *dir  = attack ? +1 : -1;
    *hold = false;
}

// 振幅を 1 段進める
/**
 * @brief エンベロープを 1 段進める。
 * @param[in,out] vol 音量（0-15）
 * @param[in,out] dir 進む向き
 * @param[in,out] hold 端で止まる状態か
 * @param shape 形状レジスタの値（下位 4bit）
 */
static inline void psg_envelope_step(int* vol, int* dir, bool* hold, uint8_t shape) {
    if (*hold) return;

    *vol += *dir;
    if (*vol >= 0 && *vol <= 15) return; // まだ端に達していない

    // 端に達した = 1 サイクル完了
    bool cont      = shape & 0x08;
    bool alternate = shape & 0x02;
    bool hbit      = shape & 0x01;

    if (!cont) {
        // Continue=0: 一度だけ動いて 0 に落ちて保持
        *vol = 0;
        *hold = true;
        return;
    }
    if (hbit) {
        // 到達端（Alternate なら反転側）で保持
        *vol = alternate ? ((*dir > 0) ? 0 : 15)
                         : ((*dir > 0) ? 15 : 0);
        *hold = true;
        return;
    }
    if (alternate) {
        // 三角波: 方向を反転して端から折り返す
        *dir = -*dir;
        *vol += *dir;
    } else {
        // ノコギリ波: 反対の端へ飛んで同方向で繰り返す
        *vol = (*dir > 0) ? 0 : 15;
    }
}

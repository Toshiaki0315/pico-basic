#include <gtest/gtest.h>
#include "psg_envelope.h"
#include <vector>

// AY-3-8910 エンベロープの純粋ロジック（src/psg_envelope.h）の検証。
// 実機の音は確認できないので、形状ごとの振幅列を固定する。

static std::vector<int> run_shape(uint8_t shape, int steps) {
    int vol, dir; bool hold;
    psg_envelope_init(&vol, &dir, &hold, shape);
    std::vector<int> out{vol};
    for (int i = 0; i < steps; i++) {
        psg_envelope_step(&vol, &dir, &hold, shape);
        out.push_back(vol);
    }
    return out;
}

TEST(PsgEnvelopeTest, Shape9_DecayThenSilence) {
    auto v = run_shape(0x09, 20); // 15→0 減衰、以後 0 保持
    EXPECT_EQ(v.front(), 15);
    EXPECT_EQ(v[15], 0);
    for (size_t i = 16; i < v.size(); i++) EXPECT_EQ(v[i], 0) << "i=" << i;
}

TEST(PsgEnvelopeTest, Shape8_RepeatingSaw) {
    auto v = run_shape(0x08, 33); // 15→0 を繰り返す
    EXPECT_EQ(v[0], 15);
    EXPECT_EQ(v[15], 0);
    EXPECT_EQ(v[16], 15) << "1 サイクル後に頭へ戻っていない";
    EXPECT_EQ(v[31], 0);
    EXPECT_EQ(v[32], 15);
}

TEST(PsgEnvelopeTest, Shape12_RepeatingRamp) {
    auto v = run_shape(0x0C, 33); // 0→15 を繰り返す
    EXPECT_EQ(v[0], 0);
    EXPECT_EQ(v[15], 15);
    EXPECT_EQ(v[16], 0) << "上昇の折り返しがおかしい";
}

TEST(PsgEnvelopeTest, Shape14_Triangle) {
    auto v = run_shape(0x0E, 33); // 0→15→0→15 の三角
    EXPECT_EQ(v[0], 0);
    EXPECT_EQ(v[15], 15);
    EXPECT_EQ(v[16], 15) << "頂点で 1 段留まる（AY の三角波）";
    EXPECT_EQ(v[31], 0);
}

TEST(PsgEnvelopeTest, Shape11_DecayThenHoldHigh) {
    auto v = run_shape(0x0B, 30); // 下降→反転端(15)で保持
    EXPECT_EQ(v[0], 15);
    EXPECT_EQ(v[15], 0);
    for (size_t i = 16; i < v.size(); i++) EXPECT_EQ(v[i], 15) << "i=" << i;
}

TEST(PsgEnvelopeTest, Shape0_OneShotDecay) {
    auto v = run_shape(0x00, 20); // Continue=0: 一度減衰して 0 保持
    EXPECT_EQ(v[0], 15);
    EXPECT_EQ(v[15], 0);
    for (size_t i = 16; i < v.size(); i++) EXPECT_EQ(v[i], 0);
}

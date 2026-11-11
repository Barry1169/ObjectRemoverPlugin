// SPDX-License-Identifier: MIT
//
// Matte: the single-channel alpha buffer that everything downstream depends on.

#include "TestCase.h"

#include <cmath>

#include "matte/Matte.h"

using namespace aibgr;

TEST(matte, startsEmpty) {
    Matte m;
    CHECK(m.empty());
    CHECK_EQ(m.width(), 0);
    CHECK_EQ(m.height(), 0);
    CHECK_NEAR(m.mean(), 0.0, 0.0);
}

TEST(matte, resizeAndFill) {
    Matte m;
    m.resize(9, 7);
    CHECK_EQ(m.width(), 9);
    CHECK_EQ(m.height(), 7);
    CHECK_EQ(static_cast<int>(m.stride()), 9);
    CHECK_EQ(static_cast<int>(m.floatCount()), 63);

    m.fill(0.25f);
    CHECK_NEAR(m.mean(), 0.25, 1e-6);
    CHECK_NEAR(m.at(4, 3), 0.25f, 1e-6);

    m.clear();
    CHECK_NEAR(m.mean(), 0.0, 1e-6);
}

TEST(matte, addressingIsRowMajor) {
    Matte m(4, 3);
    m.clear();
    m.at(3, 2) = 1.0f;
    CHECK_NEAR(m.at(3, 2), 1.0f, 0.0);
    CHECK_NEAR(m.at(2, 2), 0.0f, 0.0);
    CHECK_NEAR(m.at(3, 1), 0.0f, 0.0);
    CHECK(m.row(2) + 3 == &m.at(3, 2));
}

TEST(matte, growOnlyCapacity) {
    Matte m;
    m.resize(128, 128);
    const std::size_t big = m.allocatedBytes();
    m.resize(4, 4);
    CHECK_EQ(m.width(), 4);
    CHECK_MSG(m.allocatedBytes() >= big, "Matte::resize gave memory back; must be grow-only");
    m.resize(128, 128);
    CHECK_EQ(m.allocatedBytes(), big);
}

TEST(matte, clamp01) {
    Matte m(4, 1);
    m.at(0, 0) = -0.5f;
    m.at(1, 0) = 0.0f;
    m.at(2, 0) = 0.5f;
    m.at(3, 0) = 1.7f;
    m.clamp01();

    CHECK_NEAR(m.at(0, 0), 0.0f, 0.0);
    CHECK_NEAR(m.at(1, 0), 0.0f, 0.0);
    CHECK_NEAR(m.at(2, 0), 0.5f, 0.0);
    CHECK_NEAR(m.at(3, 0), 1.0f, 0.0);
}

TEST(matte, meanOfAHardMatteIsTheForegroundFraction) {
    Matte m(10, 10);
    m.clear();
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 5; ++x) {
            m.at(x, y) = 1.0f;
        }
    }
    CHECK_NEAR(m.mean(), 0.5, 1e-6);
}

TEST(matte, ambiguousFractionCountsTheSoftBand) {
    // The fraction of pixels strictly between lo and hi. This is the number that tells an artist
    // whether a matte is a confident silhouette or a mushy guess.
    Matte m(10, 1);
    for (int x = 0; x < 10; ++x) {
        m.at(x, 0) = static_cast<float>(x) / 9.0f; // 0.0 .. 1.0
    }

    const double frac = m.ambiguousFraction(0.05, 0.95);
    CHECK(frac > 0.0 && frac < 1.0);
    // x = 0 -> 0.0 (outside), x = 9 -> 1.0 (outside), the other eight are inside.
    CHECK_NEAR(frac, 0.8, 1e-6);

    // A fully hard matte has no ambiguous pixels at all.
    Matte hard(8, 8);
    hard.fill(1.0f);
    CHECK_NEAR(hard.ambiguousFraction(), 0.0, 1e-9);
}

TEST(matte, releaseFreesTheBuffer) {
    Matte m(64, 64);
    CHECK(m.allocatedBytes() > 0);
    m.release();
    CHECK_EQ(static_cast<int>(m.allocatedBytes()), 0);
    CHECK(m.empty());
    m.resize(8, 8); // usable again
    CHECK_EQ(m.width(), 8);
}

TEST(matte, degenerateSizes) {
    Matte m(4, 4);
    m.resize(0, 4);
    CHECK(m.empty());
    m.resize(4, 0);
    CHECK(m.empty());
    m.resize(-1, 4);
    CHECK(m.empty());
    CHECK_NEAR(m.mean(), 0.0, 0.0);
}

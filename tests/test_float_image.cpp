// SPDX-License-Identifier: MIT
//
// FloatImage is the working buffer for every frame. Its grow-only allocation policy is what makes
// "avoid per-frame allocations" true, so that policy is tested directly rather than assumed.

#include "TestCase.h"

#include <cstddef>

#include "core/FloatImage.h"

using namespace aibgr;

TEST(floatImage, startsEmpty) {
    FloatImage img;
    CHECK(img.empty());
    CHECK_EQ(img.width(), 0);
    CHECK_EQ(img.height(), 0);
    CHECK_EQ(static_cast<int>(img.floatCount()), 0);
}

TEST(floatImage, resizeSetsDimensionsAndStride) {
    FloatImage img;
    img.resize(7, 5);
    CHECK_EQ(img.width(), 7);
    CHECK_EQ(img.height(), 5);
    CHECK_EQ(static_cast<int>(img.stride()), 7 * FloatImage::kChannels);
    CHECK_EQ(static_cast<int>(img.floatCount()), 7 * 5 * FloatImage::kChannels);
    CHECK(!img.empty());
}

TEST(floatImage, pixelAddressingIsRowMajorRgba) {
    FloatImage img(3, 2);
    img.clearToTransparent();

    float* p = img.pixel(2, 1);
    p[0] = 1.0f;
    p[1] = 2.0f;
    p[2] = 3.0f;
    p[3] = 4.0f;

    // Every other pixel is still zero, so the write landed in exactly one place.
    CHECK_EXACT(img.pixel(2, 1)[0], 1.0f);
    CHECK_EXACT(img.pixel(2, 1)[3], 4.0f);
    CHECK_EXACT(img.pixel(1, 1)[0], 0.0f);
    CHECK_EXACT(img.pixel(2, 0)[0], 0.0f);
    CHECK_EXACT(img.pixel(0, 0)[0], 0.0f);

    // row(y) + x * 4 must agree with pixel(x, y).
    CHECK(img.row(1) + 2 * FloatImage::kChannels == img.pixel(2, 1));
}

TEST(floatImage, fillSetsEveryPixel) {
    FloatImage img(4, 3);
    img.fill(0.1f, 0.2f, 0.3f, 0.4f);
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const float* p = img.pixel(x, y);
            CHECK_EXACT(p[0], 0.1f);
            CHECK_EXACT(p[1], 0.2f);
            CHECK_EXACT(p[2], 0.3f);
            CHECK_EXACT(p[3], 0.4f);
        }
    }
}

TEST(floatImage, growOnlyNeverShrinksCapacity) {
    FloatImage img;
    img.resize(64, 64);
    const std::size_t big = img.allocatedBytes();
    CHECK(big >= 64u * 64u * 4u * sizeof(float));

    // Shrinking the logical size must NOT give the memory back: the next frame of a sequence is
    // usually the same size again, and reallocating every frame is exactly what the performance
    // requirements forbid.
    img.resize(8, 8);
    CHECK_EQ(img.width(), 8);
    CHECK_EQ(img.height(), 8);
    CHECK_MSG(img.allocatedBytes() >= big, "resize() gave memory back; policy must be grow-only");

    // Growing back to the previous size must therefore not reallocate.
    img.resize(64, 64);
    CHECK_EQ(img.allocatedBytes(), big);
}

TEST(floatImage, steadyStateRenderLoopAllocatesNothing) {
    // The claim under test: after warm-up, repeatedly processing the same size performs zero
    // allocation. Verified by watching allocatedBytes() stay flat across many resizes.
    FloatImage img;
    img.resize(1920, 1080); // warm-up
    const std::size_t warm = img.allocatedBytes();

    for (int frame = 0; frame < 50; ++frame) {
        img.resize(1920, 1080);
        img.fill(0.5f, 0.5f, 0.5f, 1.0f);
        CHECK_EQ(img.allocatedBytes(), warm);
    }
}

TEST(floatImage, growingPreservesNothingAndIsDocumented) {
    // After a resize that exceeds capacity the contents are UNDEFINED - callers must fill() or write
    // every pixel. This test asserts only that the geometry is right and that a subsequent fill()
    // reaches every pixel, which is the contract callers rely on.
    FloatImage img(2, 2);
    img.fill(9.0f, 9.0f, 9.0f, 9.0f);
    img.resize(5, 5);
    CHECK_EQ(img.width(), 5);
    CHECK_EQ(img.height(), 5);
    img.fill(1.0f, 1.0f, 1.0f, 1.0f);
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            CHECK_EXACT(img.pixel(x, y)[0], 1.0f);
        }
    }
}

TEST(floatImage, zeroAndNegativeSizesAreEmpty) {
    FloatImage img(4, 4);
    img.resize(0, 4);
    CHECK(img.empty());
    img.resize(4, -1);
    CHECK(img.empty());
    img.resize(-3, -3);
    CHECK(img.empty());
}

TEST(floatImage, releaseActuallyFreesTheBuffer) {
    FloatImage img(256, 256);
    CHECK(img.allocatedBytes() > 0);
    img.release();
    CHECK_EQ(static_cast<int>(img.allocatedBytes()), 0);
    CHECK(img.empty());
    // And it is usable again afterwards - the inspector's Clear Cache button relies on that.
    img.resize(16, 16);
    CHECK_EQ(img.width(), 16);
    CHECK(img.allocatedBytes() > 0);
}

TEST(floatImage, channelCountIsFour) {
    CHECK_EQ(FloatImage::kChannels, 4);
}

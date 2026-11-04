// SPDX-License-Identifier: MIT
#include "TestCase.h"
#include "guidance/BrushStrokes.h"
using namespace aibgr;
TEST(brush, roundtripRejectsMalformedAndOversizedData) {
    std::vector<BrushStroke> strokes{{true, 5, {{10,10},{20,10}}}, {false, 4, {{16,10}}}};
    std::vector<BrushStroke> parsed;
    CHECK(parseBrushStrokes(serialiseBrushStrokes(strokes), parsed));
    CHECK_EQ(parsed.size(), size_t(2)); CHECK_EQ(parsed[0].points.size(), size_t(2));
    CHECK(!parseBrushStrokes("AIBGR1 1 K 3 1 nan 2", parsed));
    CHECK(parsed.empty());
    CHECK(!parseBrushStrokes("AIBGR1 999999999", parsed));
    CHECK(!parseBrushStrokes(std::string(200001, 'a'), parsed));
}
TEST(brush, geometrySupportsOriginProxyAndLaterRemove) {
    std::vector<BrushStroke> strokes{{true, 5, {{100,100},{120,100}}}, {false, 2, {{110,100}}}};
    FrameContext ctx; ctx.renderScaleX = ctx.renderScaleY = 0.5; ctx.renderWindow = {40,40,100,100};
    BrushCorrections brush; brush.configure(strokes, ctx);
    Matte matte(60,60); matte.fill(0.5f);
    CHECK(brush.apply(matte).ok());
    CHECK_NEAR(matte.at(11,10), 1.0, 1e-6);
    CHECK_NEAR(matte.at(15,10), 0.0, 1e-6);
    CHECK_NEAR(matte.at(30,30), 0.5, 1e-6);
    matte.fill(0.5f); CHECK(brush.apply(matte, true).ok());
    CHECK_NEAR(matte.at(11,10), 0.0, 1e-6);
    CHECK_NEAR(matte.at(15,10), 1.0, 1e-6);
}

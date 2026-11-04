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

#include "guidance/GuidedSelection.h"
TEST(brush, guidedSelectionInfersUnmarkedPixelsAndHonoursOutline) {
    FloatImage image(80,60);
    for (int y=0;y<60;++y) for(int x=0;x<80;++x) {
        float* p=image.pixel(x,y); const bool subject=x>=20 && x<60 && y>=10 && y<50;
        p[0]=subject ? 0.8f : 0.1f; p[1]=subject ? 0.1f : 0.2f; p[2]=subject ? 0.1f : 0.9f; p[3]=1;
    }
    FrameContext ctx; ctx.renderWindow={0,0,80,60};
    BrushStroke outline{true,2,{{15,5},{65,5},{65,55},{15,55}},true};
    BrushStroke keep{true,3,{{25,20}}};
    std::vector<BrushStroke> strokes{outline,keep};
    std::vector<BrushStroke> roundtrip;
    CHECK(parseBrushStrokes(serialiseBrushStrokes(strokes),roundtrip));
    CHECK(roundtrip[0].outline); CHECK(!roundtrip[1].outline);
    CHECK(!parseBrushStrokes("AIBGR1 1 L 3 3 0 0 1 0 1 1",roundtrip));
    GuidedSelection select; select.configure(strokes,ctx);
    Matte matte(80,60); matte.fill(0.1f);
    CHECK(select.refine(image,matte,false).ok());
    CHECK(matte.at(45,40)>0.7f); // unmarked subject recovered from colour samples
    CHECK(matte.at(5,5)<0.01f); // outside rough outline is background
    Matte inverted(80,60); inverted.fill(0.9f);
    CHECK(select.refine(image,inverted,true).ok());
    CHECK_NEAR(matte.at(45,40),1-inverted.at(45,40),1e-5);
    CHECK_NEAR(matte.at(5,5),1-inverted.at(5,5),1e-5);
}
TEST(brush, guidedWithoutBothSeedClassesKeepsPrior) {
    FloatImage image(8,8); image.fill(0.5f,0.5f,0.5f,1.0f);
    Matte matte(8,8); matte.fill(0.3f);
    FrameContext ctx; ctx.renderWindow={0,0,8,8};
    GuidedSelection select; select.configure({{true,2,{{3,3}}}},ctx);
    CHECK(select.refine(image,matte,false).ok());
    CHECK_NEAR(matte.at(7,7),0.3,1e-6);
}

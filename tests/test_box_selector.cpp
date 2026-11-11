// SPDX-License-Identifier: MIT
#include "TestCase.h"
#include "inference/BoxSelector.h"
#include <cstdlib>
#include <fstream>
using namespace aibgr;
TEST(boxSelection, rejectsMissingModels) {
    BoxSelector selector; CHECK(!selector.open("missing","missing","missing").ok());
    FloatImage im(2,2); Matte m(2,2); CHECK(!selector.refine(im,m).ok());
}
TEST(boxSelection, realTreeImageWhenProvided) {
    const char* runtime=std::getenv("AIBGR_TEST_ORT");
    const char* enc=std::getenv("AIBGR_TEST_BOX_ENCODER"), *dec=std::getenv("AIBGR_TEST_BOX_DECODER");
    const char* raw=std::getenv("AIBGR_TEST_BOX_TREE_RAW");
    if(!runtime||!enc||!dec||!raw) return;
    FloatImage image(1024,574); std::ifstream in(raw,std::ios::binary);
    in.read(reinterpret_cast<char*>(image.data()),static_cast<std::streamsize>(image.floatCount()*sizeof(float)));
    CHECK(in.good()); if(!in.good()) return;
    BoxSelector selector; auto loaded=selector.open(runtime,enc,dec); CHECK_MSG(loaded.ok(),loaded.toString()); if(!loaded.ok()) return;
    FrameContext ctx; ctx.renderWindow={0,0,1024,574};
    BrushStroke box{true,1,{{100,4},{825,573}},false,true};
    selector.configure({box},ctx); Matte result(1024,574); result.fill(0.5f);
    auto r=selector.refine(image,result); CHECK_MSG(r.ok(),r.toString()); if(!r.ok())return;
    CHECK(result.at(500,250)>0.9f); CHECK(result.at(40,400)<0.1f); CHECK(result.at(300,125)>0.9f);
    // Alternate hypothesis recovers part of the missing left foliage on this supplied image.
    selector.configure({box},ctx,true); Matte detail(1024,574); detail.fill(0.5f);
    CHECK(selector.refine(image,detail).ok());
    size_t gained=0, foliage=0, lost=0;
    for(int y=0;y<574;++y) for(int x=0;x<1024;++x) {
        if(detail.at(x,y)>result.at(x,y)+0.1f) { ++gained; if(x>=120&&x<330&&y>=280&&y<450) ++foliage; }
        if(detail.at(x,y)+0.001f<result.at(x,y)) ++lost;
    }
    CHECK(gained>100); CHECK(foliage>100); CHECK_EQ(lost,size_t(0)); CHECK(detail.at(40,400)<0.1f);
    selector.configure({box},ctx); Matte normalAgain(1024,574);
    CHECK(selector.refine(image,normalAgain).ok()); CHECK_NEAR(normalAgain.at(500,250),result.at(500,250),1e-5);
    BrushStroke removeLeft{false,1,{{249,14},{366,164}},false,true};
    selector.configure({box,removeLeft},ctx); Matte removed(1024,574); removed.fill(0.5f);
    CHECK(selector.refine(image,removed).ok());
    CHECK(removed.at(300,125)<0.1f); CHECK(removed.at(500,250)>0.9f);
    BrushStroke addLeft=removeLeft; addLeft.keep=true;
    selector.configure({box,removeLeft,addLeft},ctx); Matte restored(1024,574); restored.fill(0.5f);
    CHECK(selector.refine(image,restored).ok()); CHECK(restored.at(300,125)>0.9f);
    // Inversion is the compositor's job. A refiner must always report SUBJECT alpha, so asking
    // for the same prompt twice gives byte-identical output (test_pipeline checks the inversion).
    selector.configure({box},ctx);
    Matte againSame(1024,574); againSame.fill(0.5f);
    CHECK(selector.refine(image,againSame).ok());
    CHECK_NEAR(againSame.at(500,250),result.at(500,250),1e-5);
    // Same canonical box at proxy scale / nonzero window produces the same mask.
    FrameContext proxy; proxy.renderScaleX=proxy.renderScaleY=0.5; proxy.renderWindow={10,20,1034,594};
    BrushStroke transformed{true,1,{{220,48},{1670,1186}},false,true};
    selector.configure({transformed},proxy); Matte again(1024,574); again.fill(0.5f);
    CHECK(selector.refine(image,again).ok()); CHECK_NEAR(again.at(500,250),result.at(500,250),1e-5);
    selector.clearCache(); CHECK(selector.refine(image,again).ok()); CHECK_NEAR(again.at(40,400),result.at(40,400),1e-5);
}

TEST(boxSelection, pointPromptIsRejectedCleanlyWithoutModels) {
    BoxSelector selector; // never opened: this is the "model files missing" path
    FrameContext ctx; ctx.renderWindow={0,0,64,64};
    selector.configure({{true,1,{{32,32}},false,false,true}}, ctx);
    FloatImage image(64,64); Matte matte(64,64);
    const Result r = selector.refine(image,matte);
    CHECK(!r.ok()); CHECK_EQ(r.status, Status::InvalidState);
}

TEST(boxSelection, realModelPointPromptWhenProvided) {
    const char* runtime=std::getenv("AIBGR_TEST_ORT");
    const char* enc=std::getenv("AIBGR_TEST_BOX_ENCODER"), *dec=std::getenv("AIBGR_TEST_BOX_DECODER");
    if(!runtime||!enc||!dec) return;
    const int W=640,H=360;
    FloatImage image(W,H);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x) {
        float* p=image.pixel(x,y);
        p[0]=0.05f; p[1]=0.07f; p[2]=0.35f; p[3]=1.0f;   // dark blue background
    }
    // Bright block in the TOP-LEFT of the picture: top-down rows 30..150, columns 120..280.
    // FloatImage and Matte rows run bottom-up, so the same rows are H-1-ytd.
    for(int ytd=30;ytd<150;++ytd) {
        const int y=H-1-ytd;
        for(int x=120;x<280;++x) { float* p=image.pixel(x,y); p[0]=0.95f; p[1]=0.55f; p[2]=0.10f; p[3]=1.0f; }
    }
    BoxSelector selector;
    auto loaded=selector.open(runtime,enc,dec); CHECK_MSG(loaded.ok(),loaded.toString());
    if(!loaded.ok()) return;
    FrameContext ctx; ctx.renderWindow={0,0,W,H};
    // Canonical coordinates are y-up: picture centre (200, ytd 90) becomes y = H-90 = 270.
    BrushStroke point{true,1,{{200,270}},false,false,true};
    selector.configure({point},ctx);
    Matte result(W,H); result.fill(0.5f);
    auto r=selector.refine(image,result); CHECK_MSG(r.ok(),r.toString()); if(!r.ok()) return;
    CHECK(result.at(200,H-1-90)>0.5f);   // the clicked subject
    CHECK(result.at(200,90)<0.1f);       // the mirrored row: a y flip would light this up
    CHECK(result.at(560,60)<0.1f);       // background
    // A remove point subtracts the object found under it, exactly like a remove box.
    BrushStroke remove{false,1,{{200,270}},false,false,true};
    selector.configure({point,remove},ctx);
    Matte removed(W,H); removed.fill(0.5f);
    CHECK(selector.refine(image,removed).ok());
    CHECK(removed.at(200,H-1-90)<0.1f);
}

TEST(boxSelection, reusePolicyReturnsCachedMatteForNewFramesButNotNewPrompts) {
    const char* runtime=std::getenv("AIBGR_TEST_ORT");
    const char* enc=std::getenv("AIBGR_TEST_BOX_ENCODER"), *dec=std::getenv("AIBGR_TEST_BOX_DECODER");
    if(!runtime||!enc||!dec) return;
    const int W=640,H=360;
    auto makeFrame=[&](float offset){
        FloatImage image(W,H);
        for(int y=0;y<H;++y) for(int x=0;x<W;++x) {
            float* p=image.pixel(x,y); p[0]=0.06f+offset; p[1]=0.08f; p[2]=0.30f; p[3]=1.0f;
        }
        for(int ytd=40;ytd<160;++ytd) {
            const int y=H-1-ytd;
            for(int x=120;x<300;++x) { float* p=image.pixel(x,y); p[0]=0.9f;p[1]=0.5f;p[2]=0.1f;p[3]=1.0f; }
        }
        return image;
    };
    FloatImage first=makeFrame(0.0f), second=makeFrame(0.04f); // a "next frame": same prompts, new pixels
    BoxSelector selector;
    auto loaded=selector.open(runtime,enc,dec); CHECK_MSG(loaded.ok(),loaded.toString());
    if(!loaded.ok()) return;
    FrameContext ctx; ctx.renderWindow={0,0,W,H};
    BrushStroke box{true,1,{{100,205},{300,335}},false,true};

    selector.configure({box},ctx);
    Matte frameA(W,H); frameA.fill(0.5f);
    CHECK(selector.refine(first,frameA).ok());
    CHECK(frameA.at(200,H-1-90)>0.5f);

    // Reuse OFF: the new frame is a real cache miss and is computed (still correct).
    Matte frameB(W,H); frameB.fill(0.5f);
    CHECK(selector.refine(second,frameB).ok());
    CHECK(frameB.at(200,H-1-90)>0.5f);

    // Reuse ON with the same prompts: the previously computed matte comes back untouched, which is
    // what makes playback responsive. Identical to frameA's result because the geometry is stable.
    selector.configure({box},ctx,false,true);
    Matte reused(W,H); reused.fill(0.5f);
    CHECK(selector.refine(second,reused).ok());
    for(size_t i=0;i<reused.floatCount();++i) CHECK_NEAR(reused.data()[i],frameB.data()[i],1e-6);

    // A new prompt must invalidate the reuse, even while playing.
    BrushStroke added{true,1,{{40,40}},false,false,true};
    selector.configure({box,added},ctx,false,true);
    Matte changed(W,H); changed.fill(0.5f);
    CHECK(selector.refine(second,changed).ok());
    CHECK(changed.at(200,H-1-90)>0.5f);
    // ... and it differs from the reused matte, i.e. the model ran again.
    size_t differences=0;
    for(size_t i=0;i<changed.floatCount();++i) if(std::abs(changed.data()[i]-reused.data()[i])>0.01f) ++differences;
    CHECK(differences>0);
}

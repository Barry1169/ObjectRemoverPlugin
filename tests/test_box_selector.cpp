// SPDX-License-Identifier: MIT
#include "TestCase.h"
#include "inference/BoxSelector.h"
#include <cstdlib>
#include <fstream>
using namespace aibgr;
TEST(boxSelection, rejectsMissingModels) {
    BoxSelector selector; CHECK(!selector.open("missing","missing","missing").ok());
    FloatImage im(2,2); Matte m(2,2); CHECK(!selector.refine(im,m,false).ok());
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
    auto r=selector.refine(image,result,false); CHECK_MSG(r.ok(),r.toString()); if(!r.ok())return;
    CHECK(result.at(500,250)>0.9f); CHECK(result.at(40,400)<0.1f); CHECK(result.at(300,125)>0.9f);
    // Alternate hypothesis recovers part of the missing left foliage on this supplied image.
    selector.configure({box},ctx,true); Matte detail(1024,574); detail.fill(0.5f);
    CHECK(selector.refine(image,detail,false).ok());
    size_t gained=0, foliage=0, lost=0;
    for(int y=0;y<574;++y) for(int x=0;x<1024;++x) {
        if(detail.at(x,y)>result.at(x,y)+0.1f) { ++gained; if(x>=120&&x<330&&y>=280&&y<450) ++foliage; }
        if(detail.at(x,y)+0.001f<result.at(x,y)) ++lost;
    }
    CHECK(gained>100); CHECK(foliage>100); CHECK_EQ(lost,size_t(0)); CHECK(detail.at(40,400)<0.1f);
    selector.configure({box},ctx); Matte normalAgain(1024,574);
    CHECK(selector.refine(image,normalAgain,false).ok()); CHECK_NEAR(normalAgain.at(500,250),result.at(500,250),1e-5);
    BrushStroke removeLeft{false,1,{{249,14},{366,164}},false,true};
    selector.configure({box,removeLeft},ctx); Matte removed(1024,574); removed.fill(0.5f);
    CHECK(selector.refine(image,removed,false).ok());
    CHECK(removed.at(300,125)<0.1f); CHECK(removed.at(500,250)>0.9f);
    BrushStroke addLeft=removeLeft; addLeft.keep=true;
    selector.configure({box,removeLeft,addLeft},ctx); Matte restored(1024,574); restored.fill(0.5f);
    CHECK(selector.refine(image,restored,false).ok()); CHECK(restored.at(300,125)>0.9f);
    selector.configure({box},ctx);
    Matte inverted(1024,574); inverted.fill(0.5f); CHECK(selector.refine(image,inverted,true).ok());
    CHECK_NEAR(result.at(500,250)+inverted.at(500,250),1.0,1e-5);
    CHECK_NEAR(result.at(40,400)+inverted.at(40,400),1.0,1e-5);
    // Same canonical box at proxy scale / nonzero window produces the same mask.
    FrameContext proxy; proxy.renderScaleX=proxy.renderScaleY=0.5; proxy.renderWindow={10,20,1034,594};
    BrushStroke transformed{true,1,{{220,48},{1670,1186}},false,true};
    selector.configure({transformed},proxy); Matte again(1024,574); again.fill(0.5f);
    CHECK(selector.refine(image,again,false).ok()); CHECK_NEAR(again.at(500,250),result.at(500,250),1e-5);
    selector.clearCache(); CHECK(selector.refine(image,again,false).ok()); CHECK_NEAR(again.at(40,400),result.at(40,400),1e-5);
}

TEST(boxSelection, briaRequiresBaseAndMissingModelIsSafe) {
    BoxSelector selector;
    CHECK(!selector.openBria("missing").ok());
    FloatImage image(3,3); Matte result(3,3); result.fill(0.7f);
    FrameContext ctx; ctx.renderWindow={0,0,3,3};
    selector.configure({BrushStroke{true,1,{{0,0},{2,2}},false,true}},ctx,false,true);
    CHECK(!selector.refine(image,result,false).ok()); CHECK_NEAR(result.at(0,0),0.7f,1e-6);
}

TEST(boxSelection, briaTreeWhenProvided) {
    const char* runtime=std::getenv("AIBGR_TEST_ORT"), *enc=std::getenv("AIBGR_TEST_BOX_ENCODER"), *dec=std::getenv("AIBGR_TEST_BOX_DECODER");
    const char* raw=std::getenv("AIBGR_TEST_BOX_TREE_RAW"), *model=std::getenv("AIBGR_TEST_BRIA");
    if(!runtime||!enc||!dec||!raw||!model) return;
    FloatImage image(1024,574); std::ifstream in(raw,std::ios::binary);
    in.read(reinterpret_cast<char*>(image.data()),static_cast<std::streamsize>(image.floatCount()*sizeof(float)));
    CHECK(in.good()); if(!in.good()) return;
    BoxSelector s; auto r=s.open(runtime,enc,dec); CHECK_MSG(r.ok(),r.toString()); if(!r.ok()) return;
    CHECK(!s.openBria("missing").ok());
    CHECK(!s.openBria(enc).ok()); // valid ONNX, wrong network/interface
    FrameContext before; before.renderWindow={0,0,1024,574};
    s.configure({BrushStroke{true,1,{{90,0},{850,574}},false,true}},before,false,true);
    Matte missing(1024,574); missing.fill(0.7f);
    CHECK(!s.refine(image,missing,false).ok()); CHECK_NEAR(missing.at(500,250),0.7f,1e-6f);
    r=s.openBria(model); CHECK_MSG(r.ok(),r.toString()); if(!r.ok()) return;
    FrameContext ctx; ctx.renderWindow={0,0,1024,574};
    BrushStroke add{true,1,{{90,0},{850,574}},false,true};
    BrushStroke remove{false,1,{{590,409},{770,554}},false,true};
    s.configure({add},ctx,false,true); Matte a(1024,574); r=s.refine(image,a,false);
    CHECK_MSG(r.ok(),r.toString()); if(!r.ok()) return;
    for(size_t i=0;i<a.floatCount();++i) CHECK(std::isfinite(a.data()[i]) && a.data()[i]>=0 && a.data()[i]<=1);
    CHECK(a.at(40,300)<0.01f); CHECK(a.at(500,250)>0.9f);
    s.configure({add,remove},ctx,false,true); Matte sub(1024,574); r=s.refine(image,sub,false);
    CHECK_MSG(r.ok(),r.toString()); if(!r.ok()) return;
    size_t removed=0, increased=0;
    for(size_t i=0;i<a.floatCount();++i) { if(sub.data()[i]+0.01f<a.data()[i]) ++removed; if(sub.data()[i]>a.data()[i]+1e-5f) ++increased; }
    CHECK(removed>100); CHECK_EQ(increased,size_t(0)); CHECK(sub.at(500,250)>0.9f);
    if(const char* dest=std::getenv("AIBGR_TEST_BRIA_RESULT")) { std::ofstream out(dest,std::ios::binary); out.write(reinterpret_cast<const char*>(sub.data()),static_cast<std::streamsize>(sub.floatCount()*sizeof(float))); CHECK(out.good()); }
    s.configure({add,remove,add},ctx,false,true); Matte restored(1024,574); r=s.refine(image,restored,false);
    CHECK_MSG(r.ok(),r.toString()); if(!r.ok()) return;
    for(size_t i=0;i<a.floatCount();++i) CHECK_NEAR(restored.data()[i],a.data()[i],1e-5f);
    s.configure({add,remove},ctx,false,true); Matte inverse(1024,574); r=s.refine(image,inverse,true);
    CHECK_MSG(r.ok(),r.toString()); if(!r.ok()) return;
    for(size_t i=0;i<a.floatCount();++i) CHECK_NEAR(inverse.data()[i]+sub.data()[i],1.0f,1e-5f);
    FrameContext proxy; proxy.renderScaleX=proxy.renderScaleY=0.5; proxy.renderWindow={10,20,1034,594};
    BrushStroke proxyAdd{true,1,{{200,40},{1720,1188}},false,true};
    BrushStroke proxyRemove{false,1,{{1200,858},{1560,1148}},false,true};
    s.configure({proxyAdd,proxyRemove},proxy,false,true); Matte proxyResult(1024,574); r=s.refine(image,proxyResult,false);
    CHECK_MSG(r.ok(),r.toString());
    CHECK_NEAR(proxyResult.at(500,250),sub.at(500,250),1e-5f);
    CHECK_NEAR(proxyResult.at(670,500),sub.at(670,500),1e-5f);
    s.configure({add,remove},ctx,false,true);
    s.clearCache(); Matte again(1024,574); r=s.refine(image,again,true); CHECK_MSG(r.ok(),r.toString());
    CHECK_NEAR(again.at(500,250),inverse.at(500,250),1e-5f);
}

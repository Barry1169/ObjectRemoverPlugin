// SPDX-License-Identifier: MIT
#include "inference/BoxSelector.h"
#include "matte/Resize.h"
#include "util/Paths.h"
#include <onnxruntime_c_api.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace aibgr {
namespace {
using GetApiBaseFn=const OrtApiBase* (ORT_API_CALL*)();
struct Value { const OrtApi* api; OrtValue* v=nullptr; ~Value(){if(v) api->ReleaseValue(v);} };
struct Shape { const OrtApi* api; OrtTensorTypeAndShapeInfo* v=nullptr; ~Shape(){if(v) api->ReleaseTensorTypeAndShapeInfo(v);} };
uint64_t hashBytes(uint64_t hash,const void* p,size_t n) { const auto* b=static_cast<const unsigned char*>(p); while(n--) { hash^=*b++; hash*=1099511628211ULL; } return hash; }
}
struct BoxSelector::Impl {
#if defined(_WIN32)
    HMODULE library=nullptr;
#else
    void* library=nullptr;
#endif
    const OrtApi* api=nullptr; OrtEnv* env=nullptr; OrtSessionOptions* options=nullptr;
    OrtSession* encoder=nullptr; OrtSession* decoder=nullptr; OrtMemoryInfo* memory=nullptr;
    OrtValue* embedding=nullptr; uint64_t embeddingHash=0; bool embeddingValid=false;
    FloatImage scaled; Matte small, full; std::vector<float> input;
    uint64_t maskHash=0; bool maskValid=false;
    ~Impl() {
        if(api) { if(embedding) api->ReleaseValue(embedding); if(memory) api->ReleaseMemoryInfo(memory);
            if(decoder) api->ReleaseSession(decoder);
            if(encoder) api->ReleaseSession(encoder);
            if(options) api->ReleaseSessionOptions(options);
            if(env) api->ReleaseEnv(env); }
#if defined(_WIN32)
        if(library) FreeLibrary(library);
#else
        if(library) dlclose(library);
#endif
    }
    void check(OrtStatus* s) { if(!s) return; std::string m=api->GetErrorMessage(s); api->ReleaseStatus(s); throw std::runtime_error(m); }
    void load(const std::string& runtime,const std::string& enc,const std::string& dec) {
#if defined(_WIN32)
        auto path=widen(runtime); library=path.empty()?nullptr:LoadLibraryExW(path.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        auto get=library?reinterpret_cast<GetApiBaseFn>(GetProcAddress(library,"OrtGetApiBase")):nullptr;
#else
        library=dlopen(runtime.c_str(),RTLD_NOW|RTLD_LOCAL);
        auto get=library?reinterpret_cast<GetApiBaseFn>(dlsym(library,"OrtGetApiBase")):nullptr;
#endif
        if(!get) throw std::runtime_error("ONNX Runtime unavailable");
        const auto* base=get(); api=base?base->GetApi(ORT_API_VERSION):nullptr;
        if(!api) throw std::runtime_error("ONNX Runtime 1.16 or newer required");
        check(api->CreateEnv(ORT_LOGGING_LEVEL_WARNING,"aibgr-box",&env));
        check(api->CreateSessionOptions(&options)); check(api->SetIntraOpNumThreads(options,2));
#if defined(_WIN32)
        const auto e=widen(enc),d=widen(dec); if(e.empty()||d.empty()) throw std::runtime_error("invalid model path");
        check(api->CreateSession(env,e.c_str(),options,&encoder)); check(api->CreateSession(env,d.c_str(),options,&decoder));
#else
        check(api->CreateSession(env,enc.c_str(),options,&encoder)); check(api->CreateSession(env,dec.c_str(),options,&decoder));
#endif
        size_t ni=0,no=0; check(api->SessionGetInputCount(encoder,&ni)); check(api->SessionGetOutputCount(encoder,&no));
        if(ni!=1||no!=1) throw std::runtime_error("unexpected box encoder interface");
        check(api->SessionGetInputCount(decoder,&ni)); check(api->SessionGetOutputCount(decoder,&no));
        if(ni!=4||no<2) throw std::runtime_error("unexpected box decoder interface");
        check(api->CreateCpuMemoryInfo(OrtArenaAllocator,OrtMemTypeDefault,&memory));
    }
    void shape(OrtValue* v,const std::vector<int64_t>& expected, ONNXTensorElementDataType dtype) {
        if (!v) throw std::runtime_error("missing box model output");
        Shape s{api}; check(api->GetTensorTypeAndShape(v,&s.v)); size_t rank=0; ONNXTensorElementDataType type;
        check(api->GetDimensionsCount(s.v,&rank)); check(api->GetTensorElementType(s.v,&type));
        if(rank!=expected.size()||type!=dtype) throw std::runtime_error("unexpected box model output type or rank");
        std::vector<int64_t> dims(rank); check(api->GetDimensions(s.v,dims.data(),rank));
        if(dims!=expected) throw std::runtime_error("unexpected box model output shape");
    }
    void encode(const FloatImage& image) {
        const double scale=std::min(1.0,1024.0/std::max(image.width(),image.height()));
        const int w=std::max(1,int(std::round(image.width()*scale))),h=std::max(1,int(std::round(image.height()*scale)));
        scaled.resize(w,h); resizeArea(image,scaled);
        uint64_t hash=1469598103934665603ULL; hash=hashBytes(hash,&w,sizeof(w)); hash=hashBytes(hash,&h,sizeof(h));
        hash=hashBytes(hash,scaled.data(),scaled.floatCount()*sizeof(float));
        if(embeddingValid && hash==embeddingHash) return;
        embeddingValid=false; maskValid=false; if(embedding) { api->ReleaseValue(embedding); embedding=nullptr; }
        input.resize(size_t(w)*h*3);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) for(int c=0;c<3;++c) {
            const float v=scaled.pixel(x,h-1-y)[c]; input[size_t(c)*w*h+size_t(y)*w+x]=std::isfinite(v)?std::clamp(v,0.0f,1.0f):0.0f;
        }
        const int64_t dims[]={1,3,h,w}; Value tensor{api};
        check(api->CreateTensorWithDataAsOrtValue(memory,input.data(),input.size()*sizeof(float),dims,4,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,&tensor.v));
        const char* in="batched_images"; const char* out="image_embeddings"; const OrtValue* value=tensor.v;
        check(api->Run(encoder,nullptr,&in,&value,1,&out,1,&embedding));
        shape(embedding,{1,256,64,64},ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
        embeddingHash=hash; embeddingValid=true;
    }
    void calculate(const FloatImage& image,const std::vector<BrushStroke>& strokes,const FrameContext& ctx,bool inverted,Matte& matte) {
        encode(image); const int w=scaled.width(),h=scaled.height();
        uint64_t hash=hashBytes(embeddingHash,&ctx.renderScaleX,sizeof(ctx.renderScaleX));
        hash=hashBytes(hash,&ctx.renderScaleY,sizeof(ctx.renderScaleY)); hash=hashBytes(hash,&ctx.pixelAspectX,sizeof(ctx.pixelAspectX));
        hash=hashBytes(hash,&ctx.renderWindow.x1,sizeof(ctx.renderWindow.x1)); hash=hashBytes(hash,&ctx.renderWindow.y1,sizeof(ctx.renderWindow.y1));
        const int iw=image.width(),ih=image.height(); hash=hashBytes(hash,&iw,sizeof(iw)); hash=hashBytes(hash,&ih,sizeof(ih));
        // Full-resolution pixels influence edge-aware upsampling even when the embedding is unchanged.
        hash=hashBytes(hash,image.data(),image.floatCount()*sizeof(float));
        hash=hashBytes(hash,&inverted,sizeof(inverted));
        const std::string key=serialiseBrushStrokes(strokes); hash=hashBytes(hash,key.data(),key.size());
        if(maskValid && hash==maskHash && full.width()==iw && full.height()==ih) { std::copy(full.data(),full.data()+full.floatCount(),matte.data()); return; }
        const double sx=ctx.renderScaleX/std::max(ctx.pixelAspectX,1e-8),sy=ctx.renderScaleY;
        if(!(sx>0 && sy>0 && std::isfinite(sx) && std::isfinite(sy))) throw std::runtime_error("invalid selection coordinates");
        small.resize(w,h); small.fill(0); size_t boxes=0; bool hasAdd=false;
        for(const auto& s:strokes) {
            if(!s.box || s.points.size()!=2) continue;
            if(++boxes>32) throw std::runtime_error("maximum 32 AI boxes exceeded");
            auto x=[&](double v){return float(std::clamp((v*sx-ctx.renderWindow.x1)*w/iw,0.0,double(w-1)));};
            auto y=[&](double v){return float(std::clamp(h-(v*sy-ctx.renderWindow.y1)*h/ih,0.0,double(h-1)));};
            const float x1=x(s.points[0].x), x2=x(s.points[1].x), y1=y(s.points[0].y), y2=y(s.points[1].y);
            if(std::abs(x1-x2)<1 || std::abs(y1-y2)<1) continue;
            if(s.keep) hasAdd=true;
            float coords[]={std::min(x1,x2),std::min(y1,y2),std::max(x1,x2),std::max(y1,y2)};
            float labels[]={2,3}; int64_t orig[]={h,w},pd[]={1,1,2,2},ld[]={1,1,2},od[]={2};
            Value pt{api},lab{api},size{api},mask{api},iou{api};
            check(api->CreateTensorWithDataAsOrtValue(memory,coords,sizeof(coords),pd,4,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,&pt.v));
            check(api->CreateTensorWithDataAsOrtValue(memory,labels,sizeof(labels),ld,3,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,&lab.v));
            check(api->CreateTensorWithDataAsOrtValue(memory,orig,sizeof(orig),od,1,ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,&size.v));
            const char* names[]={"image_embeddings","batched_point_coords","batched_point_labels","orig_im_size"};
            const OrtValue* values[]={embedding,pt.v,lab.v,size.v}; const char* outs[]={"output_masks","iou_predictions"};
            OrtValue* results[]={nullptr,nullptr}; OrtStatus* status=api->Run(decoder,nullptr,names,values,4,outs,2,results);
            mask.v=results[0]; iou.v=results[1]; check(status);
            shape(mask.v,{1,1,3,h,w},ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT); shape(iou.v,{1,1,3},ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
            float *pred=nullptr,*scores=nullptr; check(api->GetTensorMutableData(mask.v,reinterpret_cast<void**>(&pred))); check(api->GetTensorMutableData(iou.v,reinterpret_cast<void**>(&scores)));
            if(!pred || !scores || !std::isfinite(scores[0]) || !std::isfinite(scores[1]) || !std::isfinite(scores[2])) throw std::runtime_error("invalid box output");
            int best=0; for(int i=1;i<3;++i) if(scores[i]>scores[best]) best=i;
            const size_t plane=size_t(w)*h; const float* result=pred+plane*best;
            for(int yy=0;yy<h;++yy) for(int xx=0;xx<w;++xx) {
                const float v=result[size_t(h-1-yy)*w+xx]; if(!std::isfinite(v)) throw std::runtime_error("non-finite selection output");
                if(v>0) small.at(xx,yy)=s.keep ? 1.0f : 0.0f;
            }
        }
        if(!hasAdd) throw std::runtime_error("draw an add box first");
        if(inverted) for(size_t i=0;i<small.floatCount();++i) small.data()[i]=1-small.data()[i];
        full.resize(iw,ih); resizeEdgeAware(small,scaled,image,full);
        maskHash=hash; maskValid=true; std::copy(full.data(),full.data()+full.floatCount(),matte.data());
    }
};
BoxSelector::BoxSelector()=default; BoxSelector::~BoxSelector()=default;
Result BoxSelector::open(const std::string& runtime,const std::string& encoder,const std::string& decoder) {
    impl_.reset(); try { if(!fileExists(runtime)||!fileExists(encoder)||!fileExists(decoder)) return Result::fail(Status::NotFound,"AI box model/runtime not installed");
        auto p=std::make_unique<Impl>(); p->load(runtime,encoder,decoder); impl_=std::move(p); return Result::success();
    } catch(const std::exception& e){return Result::fail(Status::BackendError,e.what());}
}
void BoxSelector::configure(std::vector<BrushStroke> strokes,FrameContext ctx) { strokes_=std::move(strokes);ctx_=ctx; }
Result BoxSelector::refine(const FloatImage& image,Matte& matte,bool inverted) {
    if(!impl_) return Result::fail(Status::InvalidState,"box model is not loaded");
    if(image.empty() || image.width()!=matte.width() || image.height()!=matte.height()) return Result::fail(Status::InvalidArgument,"image/matte mismatch");
    try { impl_->calculate(image,strokes_,ctx_,inverted,matte); return Result::success(); }
    catch(const std::exception& e) { return Result::fail(Status::BackendError,e.what()); }
}
void BoxSelector::clearCache() noexcept { if(impl_) { impl_->maskValid=false;impl_->embeddingValid=false; if(impl_->embedding){impl_->api->ReleaseValue(impl_->embedding);impl_->embedding=nullptr;} impl_->scaled.release();impl_->small.release();impl_->full.release();std::vector<float>().swap(impl_->input); } }
} // namespace aibgr

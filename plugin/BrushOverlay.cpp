// SPDX-License-Identifier: MIT
#include "plugin/BrushOverlay.h"
#include "guidance/BrushStrokes.h"
#include "plugin/HostBridge.h"
#include "plugin/ParamDefs.h"
#include "plugin/ParamValues.h"
#include "util/Log.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define AIBGR_GL_CALL __stdcall
#else
#include <dlfcn.h>
#define AIBGR_GL_CALL
#endif

namespace aibgr::ofx {
bool toolbarViewportTransform(const double model[16], const double proj[16],
                              const int viewport[4], double& x, double& y,
                              double& sx, double& sy) noexcept {
    if (viewport[2]<=0 || viewport[3]<=0) return false;
    double matrix[16]{};
    for(int col=0;col<4;++col) for(int row=0;row<4;++row)
        for(int k=0;k<4;++k) matrix[col*4+row]+=proj[k*4+row]*model[col*4+k];
    const double q=matrix[15];
    if(!std::isfinite(q) || std::abs(q)<1e-12 || std::abs(matrix[3])>1e-12 || std::abs(matrix[7])>1e-12) return false;
    const double a=viewport[2]*0.5*matrix[0]/q, b=viewport[2]*0.5*matrix[4]/q;
    const double c=viewport[3]*0.5*matrix[1]/q, d=viewport[3]*0.5*matrix[5]/q;
    if(!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(c)||!std::isfinite(d)||
       a<=0||d<=0||std::abs(b)>a*0.001||std::abs(c)>d*0.001) return false;
    const double px=viewport[0]+viewport[2]*0.5*(matrix[12]/q+1.0);
    const double py=viewport[1]+viewport[3]*0.5*(matrix[13]/q+1.0);
    const double tx=(viewport[0]+8.0-px)/a, ty=(viewport[1]+8.0-py)/d;
    const double tsx=1/a, tsy=1/d;
    if(!std::isfinite(tx)||!std::isfinite(ty)||tsx>1000||tsy>1000) return false;
    x=tx; y=ty; sx=tsx; sy=tsy; return true;
}
namespace {
struct Toolbar { double x=0, y=0, sx=1, sy=1; };
struct OverlayState { std::mutex mutex; bool dragging = false; bool buttonDown = false; bool overflow = false; bool toolbarValid = false; Toolbar toolbar; BrushStroke current; };

bool readStrokes(OfxParamSetHandle params, double time, std::vector<BrushStroke>& strokes) {
    const ParamReader p(HostBridge::instance().params(), params);
    std::string data;
    if (!p.getString(pname::kBrushData, time, data)) return false;
    return parseBrushStrokes(data, strokes);
}
bool writeStrokes(OfxParamSetHandle params, const std::vector<BrushStroke>& strokes) {
    const auto* suite = HostBridge::instance().params();
    if (!suite || !suite->paramSetValue) return false;
    const ParamReader reader(suite, params);
    const OfxParamHandle param = reader.handle(pname::kBrushData);
    if (!param) return false;
    const std::string data = serialiseBrushStrokes(strokes);
    return suite->paramSetValue(param, data.c_str()) == kOfxStatOK;
}

// Legacy OFX overlay drawing uses the host's OpenGL context. Resolve may report overlay support
// without providing a GL context; in that case never issue GL calls and log once. Loading symbols
// dynamically avoids a hard dependency in the plugin and keeps the core usable in headless hosts.
struct Gl {
    using GetContext = void* (AIBGR_GL_CALL*)();
    using Begin = void (AIBGR_GL_CALL*)(unsigned int);
    using End = void (AIBGR_GL_CALL*)();
    using Vertex = void (AIBGR_GL_CALL*)(double,double);
    using Colour = void (AIBGR_GL_CALL*)(float,float,float,float);
    using Width = void (AIBGR_GL_CALL*)(float);
    using Push = void (AIBGR_GL_CALL*)(unsigned int);
    using Disable = void (AIBGR_GL_CALL*)(unsigned int);
    using GetDouble = void (AIBGR_GL_CALL*)(unsigned int, double*);
    using GetInt = void (AIBGR_GL_CALL*)(unsigned int, int*);
#if defined(_WIN32)
    HMODULE library = nullptr;
#else
    void* library = nullptr;
#endif
    GetContext getContext = nullptr; Begin begin = nullptr; End end = nullptr;
    Vertex vertex = nullptr; Colour colour = nullptr; Width width = nullptr; Push push = nullptr; End pop = nullptr;
    Disable disable = nullptr; GetDouble getDouble = nullptr; GetInt getInt = nullptr;
    Gl() {
#if defined(_WIN32)
        library = LoadLibraryW(L"opengl32.dll");
        auto sym = [this](const char* n) -> void* { return library ? reinterpret_cast<void*>(GetProcAddress(library,n)) : nullptr; };
        getContext = reinterpret_cast<GetContext>(sym("wglGetCurrentContext"));
#else
        library = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
        auto sym = [this](const char* n) -> void* { return library ? dlsym(library,n) : nullptr; };
        getContext = reinterpret_cast<GetContext>(sym("glXGetCurrentContext"));
#endif
        begin = reinterpret_cast<Begin>(sym("glBegin")); end = reinterpret_cast<End>(sym("glEnd"));
        vertex = reinterpret_cast<Vertex>(sym("glVertex2d")); colour = reinterpret_cast<Colour>(sym("glColor4f"));
        width = reinterpret_cast<Width>(sym("glLineWidth")); push = reinterpret_cast<Push>(sym("glPushAttrib"));
        pop = reinterpret_cast<End>(sym("glPopAttrib")); disable = reinterpret_cast<Disable>(sym("glDisable"));
        getDouble = reinterpret_cast<GetDouble>(sym("glGetDoublev")); getInt = reinterpret_cast<GetInt>(sym("glGetIntegerv"));
    }
    ~Gl() {
#if defined(_WIN32)
        if (library) FreeLibrary(library);
#else
        if (library) dlclose(library);
#endif
    }
    bool usable() const { return getContext && getContext() && begin && end && vertex && colour && width && push && pop && disable; }
};
bool toolbarGeometry(OfxImageEffectHandle effect, double time, const Props& args, const Props& props, Toolbar& bar) {
    auto* fx=HostBridge::instance().effect();
    if (!fx || !fx->clipGetHandle || !fx->clipGetRegionOfDefinition) return false;
    OfxImageClipHandle clip=nullptr; OfxRectD rod{};
    if (fx->clipGetHandle(effect, kOfxImageEffectSimpleSourceClipName, &clip, nullptr)!=kOfxStatOK || !clip ||
        fx->clipGetRegionOfDefinition(clip,time,&rod)!=kOfxStatOK) return false;
    bar.sx=args.getDouble(kOfxInteractPropPixelScale,0,props.getDouble(kOfxInteractPropPixelScale,0,0));
    bar.sy=args.getDouble(kOfxInteractPropPixelScale,1,props.getDouble(kOfxInteractPropPixelScale,1,0));
    if (!std::isfinite(bar.sx) || !std::isfinite(bar.sy) || bar.sx<=0 || bar.sy<=0 ||
        !std::isfinite(rod.x1) || !std::isfinite(rod.y1) || !std::isfinite(rod.x2) || !std::isfinite(rod.y2) ||
        rod.x2<=rod.x1 || rod.y2<=rod.y1 || bar.sx>1000 || bar.sy>1000) return false;
    bar.x=rod.x1+8*bar.sx; bar.y=rod.y1+8*bar.sy; return true;
}
// Prefer the actual viewport origin instead of the image corner. At high zoom the image
// corner is often off screen. Cache this geometry at Draw, where a GL context is available,
// and reuse it for hit testing. Fall back to the source ROD if the host lacks legacy GL.
bool viewportToolbar(Toolbar& bar) {
    static Gl gl;
    if (!gl.usable() || !gl.getDouble || !gl.getInt) return false;
    double model[16]{}, proj[16]{}; int viewport[4]{};
    gl.getDouble(0x0BA6u, model); // GL_MODELVIEW_MATRIX
    gl.getDouble(0x0BA7u, proj);  // GL_PROJECTION_MATRIX
    gl.getInt(0x0BA2u, viewport); // GL_VIEWPORT
    return toolbarViewportTransform(model,proj,viewport,bar.x,bar.y,bar.sx,bar.sy);
}
constexpr int kButtonWidth=120, kButtonHeight=36, kToolbarColumns=3;
int toolbarHit(const Toolbar& b, OfxPointD p) {
    const double x=(p.x-b.x)/b.sx, y=(p.y-b.y)/b.sy;
    if (!std::isfinite(x) || !std::isfinite(y) || x<0 || x>=kToolbarColumns*kButtonWidth || y<0 || y>=2*kButtonHeight) return -1;
    const int col=int(x/kButtonWidth), row=int(y/kButtonHeight);
    if (x-col*kButtonWidth>=kButtonWidth-2 || y-row*kButtonHeight>=kButtonHeight-2) return -1;
    return row==1 ? col : col+3;
}
// Tiny built-in bitmap alphabet avoids relying on a system font or host GL text support.
const unsigned char* letter(char c) {
    static const unsigned char blank[7]={0,0,0,0,0,0,0};
    switch(c) {
    case 'A': {static const unsigned char p[7]={14,17,17,31,17,17,17};return p;}
    case 'B': {static const unsigned char p[7]={30,17,17,30,17,17,30};return p;}
    case 'C': {static const unsigned char p[7]={14,17,16,16,16,17,14};return p;}
    case 'D': {static const unsigned char p[7]={30,17,17,17,17,17,30};return p;}
    case 'E': {static const unsigned char p[7]={31,16,16,30,16,16,31};return p;}
    case 'F': {static const unsigned char p[7]={31,16,16,30,16,16,16};return p;}
    case 'I': {static const unsigned char p[7]={31,4,4,4,4,4,31};return p;}
    case 'L': {static const unsigned char p[7]={16,16,16,16,16,16,31};return p;}
    case 'M': {static const unsigned char p[7]={17,27,21,21,17,17,17};return p;}
    case 'N': {static const unsigned char p[7]={17,25,21,19,17,17,17};return p;}
    case 'O': {static const unsigned char p[7]={14,17,17,17,17,17,14};return p;}
    case 'R': {static const unsigned char p[7]={30,17,17,30,20,18,17};return p;}
    case 'S': {static const unsigned char p[7]={15,16,16,14,1,1,30};return p;}
    case 'T': {static const unsigned char p[7]={31,4,4,4,4,4,4};return p;}
    case 'U': {static const unsigned char p[7]={17,17,17,17,17,17,14};return p;}
    case 'V': {static const unsigned char p[7]={17,17,17,17,17,10,4};return p;}
    case 'X': {static const unsigned char p[7]={17,17,10,4,10,17,17};return p;}
    case '+': {static const unsigned char p[7]={0,4,4,31,4,4,0};return p;}
    default:return blank;
    }
}
void drawToolbar(const Toolbar& b, int mode, int guided, bool detail) {
    static Gl gl;
    if (!gl.usable()) return;
    const char* labels[]={"ADD BOX", "REMOVE BOX", detail ? "DETAIL ON" : "DETAIL OFF", "UNDO", "CLEAR", "OFF"};
    gl.push(0x00000001u | 0x00000004u | 0x00002000u);
    gl.disable(0x0B71u); gl.disable(0x0B50u); gl.disable(0x0B44u);
    gl.disable(0x0BC0u); gl.disable(0x0DE1u); gl.disable(0x0BE2u);
    gl.width(2.0f);
    for(int i=0;i<6;++i) {
        const double x=b.x+(i%3)*kButtonWidth*b.sx, y=b.y+(i<3 ? kButtonHeight : 0)*b.sy;
        const bool selected=(i==0&&mode==4&&(guided==2||guided==3))||(i==1&&mode==5&&guided==2)||(i==2&&detail)||(i==5&&mode==0);
        gl.colour(0.045f,0.055f,0.07f,1.0f);
        gl.begin(0x0007u); gl.vertex(x,y); gl.vertex(x+118*b.sx,y); gl.vertex(x+118*b.sx,y+34*b.sy); gl.vertex(x,y+34*b.sy); gl.end();
        if(selected) gl.colour(1.0f,0.8f,0.1f,1); else gl.colour(0.65f,0.7f,0.75f,1);
        gl.begin(0x0002u); gl.vertex(x,y); gl.vertex(x+118*b.sx,y); gl.vertex(x+118*b.sx,y+34*b.sy); gl.vertex(x,y+34*b.sy); gl.end();
        if(i==0) gl.colour(0.35f,1.0f,0.45f,1); else if(i==1) gl.colour(1.0f,0.4f,0.4f,1); else gl.colour(1,1,1,1);
        constexpr double scale=1.7; const double textWidth=(std::strlen(labels[i])*6-1)*scale;
        const double left=(118-textWidth)/2, bottom=(34-7*scale)/2;
        gl.begin(0x0007u);
        for(size_t k=0;labels[i][k];++k) {
            const unsigned char* rows=letter(labels[i][k]);
            for(int r=0;r<7;++r) for(int c=0;c<5;++c) if(rows[r]&(1u<<(4-c))) {
                const double x1=x+(left+(k*6+c)*scale)*b.sx, y1=y+(bottom+(6-r)*scale)*b.sy;
                gl.vertex(x1,y1);gl.vertex(x1+scale*b.sx,y1);gl.vertex(x1+scale*b.sx,y1+scale*b.sy);gl.vertex(x1,y1+scale*b.sy);
            }
        }
        gl.end();
    }
    gl.pop();
}
void drawStrokes(const std::vector<BrushStroke>& strokes) {
    static Gl gl;
    if (!gl.usable()) { static std::atomic<bool> reported{false}; if (!reported.exchange(true)) { AIBGR_LOGI("viewer overlay: OpenGL context unavailable; no brush overlay drawn"); } return; }
    gl.push(0x00000001u | 0x00000004u); // GL_CURRENT_BIT | GL_LINE_BIT
    gl.width(2.0f);
    for (const auto& s : strokes) {
        gl.colour(s.outline ? 1.0f : s.keep ? 0.0f : 1.0f, s.outline || s.keep ? 1.0f : 0.0f, 0.0f, 1.0f);
        if (s.box && s.points.size()==2) {
            const auto a=s.points[0],b=s.points[1]; gl.begin(0x0002u);
            gl.vertex(a.x,a.y); gl.vertex(b.x,a.y); gl.vertex(b.x,b.y); gl.vertex(a.x,b.y); gl.end();
        } else {
            gl.begin(0x0003u); // GL_LINE_STRIP
            for (const auto& p : s.points) gl.vertex(p.x,p.y);
            gl.end();
        }
        if (s.outline || s.box) continue;
        for (const auto& p : s.points) {
            gl.begin(0x0002u); // GL_LINE_LOOP
            for (int j=0; j<20; ++j) {
                const double angle = j * (6.283185307179586 / 20.0);
                gl.vertex(p.x + s.radius*std::cos(angle), p.y + s.radius*std::sin(angle));
            }
            gl.end();
        }
    }
    gl.pop();
}
}

void editSavedBrush(OfxParamSetHandle params, double time, bool clear) noexcept {
    try {
        std::vector<BrushStroke> strokes;
        if (!clear && !readStrokes(params, time, strokes)) { AIBGR_LOGE("could not read viewer brush strokes"); return; }
        if (clear) strokes.clear();
        else if (!strokes.empty()) strokes.pop_back();
        if (!writeStrokes(params, strokes)) AIBGR_LOGE("could not update viewer brush strokes");
    } catch (...) { AIBGR_LOGE("viewer brush edit failed"); }
}

OfxStatus brushOverlayMain(const char* action, const void* handle, OfxPropertySetHandle inArgs,
                           OfxPropertySetHandle) noexcept {
    try {
        auto& bridge = HostBridge::instance();
        const auto* suite = bridge.interact();
        if (!action || !handle || !suite || !suite->interactGetPropertySet || !bridge.props()) return kOfxStatReplyDefault;
        const auto interact = reinterpret_cast<OfxInteractHandle>(const_cast<void*>(handle));
        OfxPropertySetHandle propsHandle = nullptr;
        if (suite->interactGetPropertySet(interact, &propsHandle) != kOfxStatOK || !propsHandle) return kOfxStatReplyDefault;
        const Props props(bridge.props(), propsHandle);
        if (!std::strcmp(action, kOfxActionDescribe)) {
            props.setString(kOfxInteractPropSlaveToParam, 0, pname::kBrushData);
            props.setString(kOfxInteractPropSlaveToParam, 1, pname::kBrushMode);
            props.setString(kOfxInteractPropSlaveToParam, 2, pname::kSelectionMode);
            props.setString(kOfxInteractPropSlaveToParam, 3, pname::kBrushRadius);
            props.setString(kOfxInteractPropSlaveToParam, 4, pname::kBoxDetailRecovery);
            return kOfxStatOK;
        }
        if (!std::strcmp(action, kOfxActionCreateInstance)) {
            auto state = std::make_unique<OverlayState>();
            if (bridge.props()->propSetPointer(propsHandle, kOfxPropInstanceData, 0, state.get()) != kOfxStatOK) return kOfxStatFailed;
            state.release(); return kOfxStatOK;
        }
        auto* state = static_cast<OverlayState*>(props.getPointer(kOfxPropInstanceData, 0));
        if (!std::strcmp(action, kOfxActionDestroyInstance)) {
            delete state; props.setPointer(kOfxPropInstanceData, 0, nullptr); return kOfxStatOK;
        }
        if (!state) return kOfxStatReplyDefault;
        auto effect = static_cast<OfxImageEffectHandle>(props.getPointer(kOfxPropEffectInstance, 0));
        OfxParamSetHandle params = nullptr;
        if (!effect || !bridge.effect() || bridge.effect()->getParamSet(effect, &params) != kOfxStatOK || !params) return kOfxStatReplyDefault;
        const Props args(bridge.props(), inArgs);
        auto penPosition = [&](OfxPointD& point) {
            return inArgs && bridge.props()->propGetDouble &&
                bridge.props()->propGetDouble(inArgs, kOfxInteractPropPenPosition, 0, &point.x) == kOfxStatOK &&
                bridge.props()->propGetDouble(inArgs, kOfxInteractPropPenPosition, 1, &point.y) == kOfxStatOK;
        };
        const double time = args.getDouble(kOfxPropTime, 0, 0);
        if (!std::strcmp(action, kOfxInteractActionDraw)) {
            std::vector<BrushStroke> strokes;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->dragging) strokes.push_back(state->current);
            }
            drawStrokes(strokes);
            Toolbar bar{};
            if (toolbarGeometry(effect,time,args,props,bar)) {
                viewportToolbar(bar); // fall back to the source corner if unavailable
                { std::lock_guard<std::mutex> lock(state->mutex); state->toolbar=bar; state->toolbarValid=true; }
                int mode=0, guided=0, detail=0; const ParamReader reader(bridge.params(),params);
                reader.getInt(pname::kBrushMode,time,mode); reader.getInt(pname::kSelectionMode,time,guided);
                reader.getInt(pname::kBoxDetailRecovery,time,detail);
                drawToolbar(bar,mode,guided,detail!=0);
            }
            return kOfxStatOK;
        }
        if (!std::strcmp(action, kOfxInteractActionLoseFocus)) {
            { std::lock_guard<std::mutex> lock(state->mutex); state->dragging = false; state->buttonDown = false; }
            if (suite->interactRedraw) suite->interactRedraw(interact);
            return kOfxStatReplyDefault;
        }
        if (!std::strcmp(action, kOfxInteractActionPenDown)) {
            OfxPointD pos{};
            if (!penPosition(pos) || !std::isfinite(pos.x) || !std::isfinite(pos.y) || std::abs(pos.x)>1000000 || std::abs(pos.y)>1000000) return kOfxStatReplyDefault;
            int mode = 0; double radius = 25;
            const ParamReader reader(bridge.params(), params);
            Toolbar bar{};
            bool valid=toolbarGeometry(effect,time,args,props,bar);
            { std::lock_guard<std::mutex> lock(state->mutex); if(state->toolbarValid) { bar=state->toolbar; valid=true; } }
            const int button=valid ? toolbarHit(bar,pos) : -1;
            if (button>=0) {
                { std::lock_guard<std::mutex> lock(state->mutex); state->buttonDown=true; }
                const auto* paramSuite=bridge.params();
                auto setChoice=[&](const char* name, int value) {
                    OfxParamHandle p=reader.handle(name);
                    return p && paramSuite && paramSuite->paramSetValue && paramSuite->paramSetValue(p,value)==kOfxStatOK;
                };
                bool ok=true;
                if(button==0 || button==1) {
                    ok=setChoice(pname::kBrushMode,button==0 ? 4 : 5);
                    ok=setChoice(pname::kSelectionMode,2) && ok;
                } else if(button==2) {
                    int detail=0; reader.getInt(pname::kBoxDetailRecovery,time,detail);
                    ok=setChoice(pname::kBoxDetailRecovery,detail ? 0 : 1);
                } else if(button==3 || button==4) editSavedBrush(params,time,button==4);
                else ok=setChoice(pname::kBrushMode,0);
                if(!ok) AIBGR_LOGE("viewer toolbar: host rejected parameter change");
                if (suite->interactRedraw) suite->interactRedraw(interact);
                return kOfxStatOK;
            }
            reader.getInt(pname::kBrushMode, time, mode);
            reader.getDouble(pname::kBrushRadius, time, radius);
            if ((mode != 1 && mode != 2 && mode != 3 && mode != 4 && mode != 5) || !std::isfinite(radius)) return kOfxStatReplyDefault;
            if (mode==4 || mode==5) {
                int selected=0; reader.getInt(pname::kSelectionMode,time,selected);
                if(selected!=2) {
                    auto param=reader.handle(pname::kSelectionMode);
                    if(!param || !bridge.params()->paramSetValue || bridge.params()->paramSetValue(param,2)!=kOfxStatOK) {
                        AIBGR_LOGE("viewer box: could not enable AI selection mode"); return kOfxStatReplyDefault;
                    }
                }
            }
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->current = BrushStroke{}; state->overflow = false; state->current.keep = (mode == 1 || mode == 4); state->current.outline = (mode == 3); state->current.box = (mode == 4 || mode == 5); state->current.radius = std::clamp(radius,1.0,200.0);
                state->current.points.push_back({pos.x,pos.y}); if (state->current.box) state->current.points.push_back({pos.x,pos.y}); state->dragging = true;
            }
            AIBGR_LOGI("viewer brush: started %s stroke", mode == 1 ? "keep" : mode == 2 ? "remove" : mode == 3 ? "outline" : mode == 4 ? "add box" : "remove box");
            if (suite->interactRedraw) suite->interactRedraw(interact);
            return kOfxStatOK;
        }
        if (!std::strcmp(action, kOfxInteractActionPenMotion) || !std::strcmp(action, kOfxInteractActionPenUp)) {
            OfxPointD pos{};
            const bool havePosition = penPosition(pos);
            const bool isUp = !std::strcmp(action, kOfxInteractActionPenUp);
            BrushStroke finished; bool overflow=false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->buttonDown) { if (isUp) state->buttonDown=false; return kOfxStatOK; }
                if (!state->dragging) return kOfxStatReplyDefault;
                const auto& last = state->current.points.back();
                if (state->current.box && havePosition && std::isfinite(pos.x) && std::isfinite(pos.y) && std::abs(pos.x)<=1000000 && std::abs(pos.y)<=1000000) {
                    state->current.points[1]={pos.x,pos.y};
                } else if (!state->current.box && havePosition && std::isfinite(pos.x) && std::isfinite(pos.y) && std::abs(pos.x)<=1000000 && std::abs(pos.y)<=1000000 &&
                    std::hypot(pos.x-last.x,pos.y-last.y) > (state->current.outline ? 10.0 : state->current.radius*0.2)) {
                    if (state->current.points.size() < kMaxPointsPerStroke) state->current.points.push_back({pos.x,pos.y});
                    else state->overflow=true;
                }
                if (isUp) { state->dragging = false; finished = state->current; overflow=state->overflow; }
            }
            if (isUp) {
                // Preserve geometry while eliminating redundant points from long, smooth drags.
                simplifyBrushStroke(finished, finished.outline ? 1.0 : std::min(1.0,finished.radius*0.08));
                auto warn = [&](const char* text) {
                    AIBGR_LOGE("viewer brush: %s", text);
                    const auto* messages=bridge.message();
                    if (messages && messages->message) messages->message(effect,kOfxMessageWarning,"viewerBrush", "%s",text);
                };
                std::vector<BrushStroke> strokes;
                if (readStrokes(params, time, strokes)) {
                    size_t count = finished.points.size();
                    for (const auto& s : strokes) count += s.points.size();
                    if (overflow) {
                        warn("Stroke discarded: too many sampled points. Draw a shorter stroke, or use Undo/Clear.");
                    } else if (finished.box && (std::abs(finished.points[0].x-finished.points[1].x)<1 || std::abs(finished.points[0].y-finished.points[1].y)<1)) {
                        warn("Draw a box around the object rather than clicking a single point.");
                    } else if (finished.outline && finished.points.size() < 3) {
                        warn("Outline needs at least three points; draw a longer outline.");
                    } else if (finished.box && std::count_if(strokes.begin(),strokes.end(),[](const BrushStroke& s){return s.box;}) >= 32) {
                        warn("AI box limit reached (32). Use Undo or Clear to remove boxes.");
                    } else if (count <= kMaxBrushPoints) {
                        strokes.push_back(std::move(finished));
                        if (!writeStrokes(params, strokes)) warn("Resolve could not save this stroke. The stroke was not applied.");
                        else AIBGR_LOGI("viewer brush: committed stroke with %zu points", strokes.back().points.size());
                    } else warn("Stroke limit reached; stroke not applied. Use Undo or Clear to make space.");
                } else warn("Saved strokes could not be read; the new stroke was not applied.");
            }
            if (suite->interactRedraw) suite->interactRedraw(interact);
            return kOfxStatOK;
        }
        return kOfxStatReplyDefault;
    } catch (...) { AIBGR_LOGE("viewer brush overlay action failed"); return kOfxStatFailed; }
}
} // namespace aibgr::ofx

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
namespace {
struct OverlayState { std::mutex mutex; bool dragging = false; bool buttonDown = false; bool overflow = false; BrushStroke current; };

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
#if defined(_WIN32)
    HMODULE library = nullptr;
#else
    void* library = nullptr;
#endif
    GetContext getContext = nullptr; Begin begin = nullptr; End end = nullptr;
    Vertex vertex = nullptr; Colour colour = nullptr; Width width = nullptr; Push push = nullptr; End pop = nullptr;
    Disable disable = nullptr;
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
struct Toolbar { double x, y, sx, sy; };
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
int toolbarHit(const Toolbar& b, OfxPointD p) {
    const double x=(p.x-b.x)/b.sx, y=(p.y-b.y)/b.sy;
    if (!std::isfinite(x) || !std::isfinite(y) || x<0 || x>=9*42 || y<0 || y>=40) return -1;
    const int button=static_cast<int>(x/42); return x-button*42<40 ? button : -1;
}
void drawToolbar(const Toolbar& b, int mode, int guided) {
    static Gl gl;
    if (!gl.usable()) return;
    // Simple line glyphs: O L K R G - + U X. Keeps the toolbar independent of fonts.
    const char* glyphs[]={"024602", "046", "04 15 36", "0402 23 35 56", "0214 46 65 53", "25", "25 13", "04 46 62", "06 24"};
    const double px[]={7,13,19,7,7,19,19};
    const double py[]={19,19,19,13,7,13,7};
    // Save/restore all GL state touched. An opaque dark backplate stays legible on pale sky.
    gl.push(0x00000001u | 0x00000004u | 0x00002000u);
    gl.disable(0x0B71u); // GL_DEPTH_TEST
    gl.disable(0x0B50u); // GL_LIGHTING
    gl.disable(0x0B44u); // GL_CULL_FACE
    gl.disable(0x0BC0u); // GL_ALPHA_TEST
    gl.disable(0x0DE1u); // GL_TEXTURE_2D
    gl.disable(0x0BE2u); // GL_BLEND
    gl.width(2.0f);
    for(int i=0;i<9;++i) {
        const double x=b.x+i*42*b.sx;
        gl.colour(0.06f,0.06f,0.06f,1.0f);
        gl.begin(0x0007u); // GL_QUADS
        gl.vertex(x,b.y); gl.vertex(x+40*b.sx,b.y); gl.vertex(x+40*b.sx,b.y+40*b.sy); gl.vertex(x,b.y+40*b.sy); gl.end();
        const bool selected=(i==0&&mode==0)||(i==1&&mode==3)||(i==2&&mode==1)||(i==3&&mode==2)||(i==4&&guided==1);
        if (selected) gl.colour(1.0f,0.8f,0.1f,1); else gl.colour(0.9f,0.9f,0.9f,1);
        gl.begin(0x0002u); gl.vertex(x,b.y); gl.vertex(x+40*b.sx,b.y); gl.vertex(x+40*b.sx,b.y+40*b.sy); gl.vertex(x,b.y+40*b.sy); gl.end();
        if (i==2) gl.colour(0.1f,1,0.2f,1); else if (i==3) gl.colour(1,0.2f,0.2f,1);
        const char* g=glyphs[i]; bool drawing=false;
        for(;*g;++g) {
            if (*g==' ') { if(drawing) gl.end(); drawing=false; continue; }
            if(!drawing) { gl.begin(0x0003u); drawing=true; }
            int j=*g-'0'; gl.vertex(x+(px[j]*1.5+0.5)*b.sx,b.y+(py[j]*1.5+0.5)*b.sy);
        }
        if(drawing) gl.end();
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
        gl.begin(0x0003u); // GL_LINE_STRIP
        for (const auto& p : s.points) gl.vertex(p.x,p.y);
        gl.end();
        if (s.outline) continue;
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
                int mode=0, guided=0; const ParamReader reader(bridge.params(),params);
                reader.getInt(pname::kBrushMode,time,mode); reader.getInt(pname::kSelectionMode,time,guided);
                drawToolbar(bar,mode,guided);
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
            const int button=toolbarGeometry(effect,time,args,props,bar) ? toolbarHit(bar,pos) : -1;
            if (button>=0) {
                { std::lock_guard<std::mutex> lock(state->mutex); state->buttonDown=true; }
                const auto* paramSuite=bridge.params();
                auto setChoice=[&](const char* name, int value) {
                    OfxParamHandle p=reader.handle(name);
                    return p && paramSuite && paramSuite->paramSetValue && paramSuite->paramSetValue(p,value)==kOfxStatOK;
                };
                bool ok=true;
                if(button<=3) {
                    const int modes[]={0,3,1,2}; ok=setChoice(pname::kBrushMode,modes[button]);
                    if(button==1) ok=setChoice(pname::kSelectionMode,1) && ok;
                } else if(button==4) {
                    int guided=0; reader.getInt(pname::kSelectionMode,time,guided);
                    ok=setChoice(pname::kSelectionMode,guided==1 ? 0 : 1);
                } else if(button==5 || button==6) {
                    reader.getDouble(pname::kBrushRadius,time,radius);
                    ok=std::isfinite(radius) && reader.setDouble(pname::kBrushRadius,time,std::clamp(radius*(button==5 ? 0.8 : 1.25),1.0,200.0));
                } else editSavedBrush(params,time,button==8);
                if(!ok) AIBGR_LOGE("viewer toolbar: host rejected parameter change");
                if (suite->interactRedraw) suite->interactRedraw(interact);
                return kOfxStatOK;
            }
            reader.getInt(pname::kBrushMode, time, mode);
            reader.getDouble(pname::kBrushRadius, time, radius);
            if ((mode != 1 && mode != 2 && mode != 3) || !std::isfinite(radius)) return kOfxStatReplyDefault;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->current = BrushStroke{}; state->overflow = false; state->current.keep = (mode == 1); state->current.outline = (mode == 3); state->current.radius = std::clamp(radius,1.0,200.0);
                state->current.points.push_back({pos.x,pos.y}); state->dragging = true;
            }
            AIBGR_LOGI("viewer brush: started %s stroke", mode == 1 ? "keep" : mode == 2 ? "remove" : "outline");
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
                if (havePosition && std::isfinite(pos.x) && std::isfinite(pos.y) && std::abs(pos.x)<=1000000 && std::abs(pos.y)<=1000000 &&
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
                    } else if (finished.outline && finished.points.size() < 3) {
                        warn("Outline needs at least three points; draw a longer outline.");
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

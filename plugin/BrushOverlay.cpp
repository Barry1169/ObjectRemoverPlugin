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
struct OverlayState { std::mutex mutex; bool dragging = false; BrushStroke current; };

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
#if defined(_WIN32)
    HMODULE library = nullptr;
#else
    void* library = nullptr;
#endif
    GetContext getContext = nullptr; Begin begin = nullptr; End end = nullptr;
    Vertex vertex = nullptr; Colour colour = nullptr; Width width = nullptr; Push push = nullptr; End pop = nullptr;
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
        pop = reinterpret_cast<End>(sym("glPopAttrib"));
    }
    ~Gl() {
#if defined(_WIN32)
        if (library) FreeLibrary(library);
#else
        if (library) dlclose(library);
#endif
    }
    bool usable() const { return getContext && getContext() && begin && end && vertex && colour && width && push && pop; }
};
void drawStrokes(const std::vector<BrushStroke>& strokes) {
    static Gl gl;
    if (!gl.usable()) { static std::atomic<bool> reported{false}; if (!reported.exchange(true)) { AIBGR_LOGI("viewer overlay: OpenGL context unavailable; no brush overlay drawn"); } return; }
    gl.push(0x00000001u | 0x00000004u); // GL_CURRENT_BIT | GL_LINE_BIT
    gl.width(2.0f);
    for (const auto& s : strokes) {
        gl.colour(s.keep ? 0.0f : 1.0f, s.keep ? 1.0f : 0.0f, 0.0f, 1.0f);
        gl.begin(0x0003u); // GL_LINE_STRIP
        for (const auto& p : s.points) gl.vertex(p.x,p.y);
        gl.end();
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
            readStrokes(params, time, strokes);
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->dragging) strokes.push_back(state->current);
            }
            drawStrokes(strokes); return kOfxStatOK;
        }
        if (!std::strcmp(action, kOfxInteractActionLoseFocus)) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->dragging = false; return kOfxStatReplyDefault;
        }
        if (!std::strcmp(action, kOfxInteractActionPenDown)) {
            int mode = 0; double radius = 25;
            const ParamReader reader(bridge.params(), params);
            reader.getInt(pname::kBrushMode, time, mode);
            reader.getDouble(pname::kBrushRadius, time, radius);
            if ((mode != 1 && mode != 2) || !std::isfinite(radius)) return kOfxStatReplyDefault;
            OfxPointD pos{};
            if (!penPosition(pos) || !std::isfinite(pos.x) || !std::isfinite(pos.y) || std::abs(pos.x)>1000000 || std::abs(pos.y)>1000000) return kOfxStatReplyDefault;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->current = BrushStroke{}; state->current.keep = (mode == 1); state->current.radius = std::clamp(radius,1.0,200.0);
                state->current.points.push_back({pos.x,pos.y}); state->dragging = true;
            }
            AIBGR_LOGI("viewer brush: started %s stroke", mode == 1 ? "keep" : "remove");
            if (suite->interactRedraw) suite->interactRedraw(interact);
            return kOfxStatOK;
        }
        if (!std::strcmp(action, kOfxInteractActionPenMotion) || !std::strcmp(action, kOfxInteractActionPenUp)) {
            OfxPointD pos{};
            const bool havePosition = penPosition(pos);
            const bool isUp = !std::strcmp(action, kOfxInteractActionPenUp);
            BrushStroke finished;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (!state->dragging) return kOfxStatReplyDefault;
                const auto& last = state->current.points.back();
                if (havePosition && std::isfinite(pos.x) && std::isfinite(pos.y) && std::abs(pos.x)<=1000000 && std::abs(pos.y)<=1000000 &&
                    state->current.points.size() < kMaxBrushPoints && std::hypot(pos.x-last.x,pos.y-last.y) > state->current.radius*0.2)
                    state->current.points.push_back({pos.x,pos.y});
                if (isUp) { state->dragging = false; finished = state->current; }
            }
            if (isUp) {
                std::vector<BrushStroke> strokes;
                if (readStrokes(params, time, strokes)) {
                    size_t count = finished.points.size();
                    for (const auto& s : strokes) count += s.points.size();
                    if (count <= kMaxBrushPoints) {
                        strokes.push_back(std::move(finished));
                        if (!writeStrokes(params, strokes)) AIBGR_LOGE("viewer brush: host rejected saved stroke");
                        else AIBGR_LOGI("viewer brush: committed stroke with %zu points", strokes.back().points.size());
                    } else AIBGR_LOGE("viewer brush: stroke limit reached");
                } else AIBGR_LOGE("viewer brush: saved strokes unavailable or malformed");
            }
            if (suite->interactRedraw) suite->interactRedraw(interact);
            return kOfxStatOK;
        }
        return kOfxStatReplyDefault;
    } catch (...) { AIBGR_LOGE("viewer brush overlay action failed"); return kOfxStatFailed; }
}
} // namespace aibgr::ofx

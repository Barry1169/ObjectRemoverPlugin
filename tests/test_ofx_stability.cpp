// SPDX-License-Identifier: MIT
// Small fake OFX host: exercises the real action dispatcher, not just the pipeline core.
#include "TestCase.h"
#include "plugin/BgRemoverPlugin.h"
#include "plugin/HostBridge.h"
#include "plugin/ParamDefs.h"
#include "plugin/ParamValues.h"
#include "plugin/PluginPaths.h"
#include "plugin/BrushOverlay.h"
#include "util/Log.h"
#include "util/Paths.h"

#include <atomic>
#include <cstring>
#include <cstdarg>
#include <chrono>
#include <filesystem>
#include <thread>
#include <map>
#include <string>
#include <vector>

using namespace aibgr::ofx;

namespace {
struct Properties {
    std::map<std::string, std::vector<std::string>> strings;
    std::map<std::string, std::vector<int>> ints;
    std::map<std::string, std::vector<double>> doubles;
    std::map<std::string, void*> pointers;
};
template <class T> void put(std::map<std::string, std::vector<T>>& m, const char* key, int index, T value) {
    auto& v = m[key];
    if (static_cast<int>(v.size()) <= index) v.resize(index + 1);
    v[index] = value;
}
template <class T> OfxStatus get(const std::map<std::string, std::vector<T>>& m, const char* key, int index, T* out) {
    auto it = m.find(key);
    if (it == m.end() || index < 0 || static_cast<int>(it->second.size()) <= index) return kOfxStatErrUnknown;
    *out = it->second[index];
    return kOfxStatOK;
}
Properties& properties(OfxPropertySetHandle p) { return *reinterpret_cast<Properties*>(p); }
OfxPropertySetHandle handle(Properties& p) { return reinterpret_cast<OfxPropertySetHandle>(&p); }

struct FakeHost {
    Properties hostProps, effectProps, args, sourceImage, outputImage;
    std::map<std::string, Properties> definitions;
    OfxPropertySuiteV1 props{};
    OfxImageEffectSuiteV1 fx{};
    OfxParameterSuiteV1 params{};
    OfxInteractSuiteV1 interact{};
    OfxMessageSuiteV1 messages{};
    OfxHost host{};
    std::vector<float> source = std::vector<float>(64 * 64 * 4, 0.5f);
    std::vector<float> output = std::vector<float>(64 * 64 * 4, 0.0f);
    int effectToken = 0, paramToken = 0, srcToken = 0, dstToken = 0;
    int paramWrites = 0, recursionDepth = 0;
    bool sourceAvailable = true;
    double tolerance = 0.25, softness = 0.08;
    int modelIndex = 0;
    std::string modelValue = "__fallback__";
    std::string guidancePath, brushData, warning;
    int brushMode = 0, selectionMode = 0, brushBehavior = 0, redraws = 0;
    double brushRadius = 6;
    OfxImageEffectHandle effect() { return reinterpret_cast<OfxImageEffectHandle>(&effectToken); }
    OfxParamSetHandle paramSet() { return reinterpret_cast<OfxParamSetHandle>(&paramToken); }
    OfxImageClipHandle src() { return reinterpret_cast<OfxImageClipHandle>(&srcToken); }
    OfxImageClipHandle dst() { return reinterpret_cast<OfxImageClipHandle>(&dstToken); }
    FakeHost();
};
FakeHost* current = nullptr;
OfxStatus captureMessage(void*, const char*, const char*, const char* format, ...) {
    va_list ap; va_start(ap, format); const char* text=va_arg(ap,const char*);
    current->warning=text ? text : ""; va_end(ap); return kOfxStatOK;
}

const void* fetch(OfxPropertySetHandle, const char* name, int version) {
    if (version != 1) return nullptr;
    if (std::strcmp(name, kOfxPropertySuite) == 0) return &current->props;
    if (std::strcmp(name, kOfxImageEffectSuite) == 0) return &current->fx;
    if (std::strcmp(name, kOfxParameterSuite) == 0) return &current->params;
    if (std::strcmp(name, kOfxInteractSuite) == 0) return &current->interact;
    if (std::strcmp(name, kOfxMessageSuite) == 0) return &current->messages;
    return nullptr;
}
OfxStatus getParameterAtTime(OfxParamHandle p, OfxTime time, ...) {
    const char* name = reinterpret_cast<const char*>(p);
    if (std::strcmp(name, pname::kFallbackKeyTolerance) != 0 &&
        std::strcmp(name, pname::kFallbackKeySoftness) != 0 &&
        std::strcmp(name, pname::kModel) != 0 &&
        std::strcmp(name, pname::kGuidancePath) != 0 &&
        std::strcmp(name, pname::kGuidedBrushBehavior) != 0 &&
        std::strcmp(name, pname::kSelectionMode) != 0 &&
        std::strcmp(name, pname::kBrushMode) != 0 &&
        std::strcmp(name, pname::kBrushRadius) != 0 &&
        std::strcmp(name, pname::kBrushData) != 0) return kOfxStatErrUnknown;
    va_list args;
    va_start(args, time);
    if (std::strcmp(name, pname::kBrushData) == 0) {
        char** result = va_arg(args, char**); *result = const_cast<char*>(current->brushData.c_str());
    } else if (std::strcmp(name, pname::kGuidedBrushBehavior) == 0) {
        int* result = va_arg(args, int*); *result = current->brushBehavior;
    } else if (std::strcmp(name, pname::kSelectionMode) == 0) {
        int* result = va_arg(args, int*); *result = current->selectionMode;
    } else if (std::strcmp(name, pname::kBrushMode) == 0) {
        int* result = va_arg(args, int*); *result = current->brushMode;
    } else if (std::strcmp(name, pname::kBrushRadius) == 0) {
        double* result = va_arg(args, double*); *result = current->brushRadius;
    } else if (std::strcmp(name, pname::kGuidancePath) == 0) {
        char** result = va_arg(args, char**);
        *result = const_cast<char*>(current->guidancePath.c_str());
    } else if (std::strcmp(name, pname::kModel) == 0) {
        if (modelUsesStrChoice()) {
            char** result = va_arg(args, char**);
            *result = const_cast<char*>(current->modelValue.c_str());
        } else {
            int* result = va_arg(args, int*);
            *result = current->modelIndex;
        }
    } else {
        double* result = va_arg(args, double*);
        *result = std::strcmp(name, pname::kFallbackKeyTolerance) == 0 ? current->tolerance : current->softness;
    }
    va_end(args);
    return kOfxStatOK;
}
OfxStatus setParameterAtTime(OfxParamHandle p, OfxTime, ...) {
    ++current->paramWrites;
    if (++current->recursionDepth < 5) {
        Properties changed;
        put(changed.strings, kOfxPropName, 0, std::string(reinterpret_cast<const char*>(p)));
        pluginMain(kOfxActionInstanceChanged, current->effect(), handle(changed), nullptr);
    }
    --current->recursionDepth;
    return kOfxStatOK;
}
OfxStatus setParameter(OfxParamHandle p, ...) {
    const char* name = reinterpret_cast<const char*>(p);
    if (std::strcmp(name, pname::kBrushData) && std::strcmp(name, pname::kBrushMode) && std::strcmp(name, pname::kSelectionMode) && std::strcmp(name, pname::kGuidedBrushBehavior)) return kOfxStatErrUnknown;
    va_list args; va_start(args, p);
    if (!std::strcmp(name, pname::kBrushData)) {
        const char* value = va_arg(args, const char*); current->brushData = value ? value : "";
    } else if (!std::strcmp(name, pname::kBrushMode)) current->brushMode=va_arg(args,int);
    else current->selectionMode=va_arg(args,int);
    va_end(args);
    ++current->paramWrites;
    // Simulate a synchronous host notification for this user-initiated change.
    Properties changed; put(changed.strings, kOfxPropName, 0, std::string(name));
    pluginMain(kOfxActionInstanceChanged, current->effect(), handle(changed), nullptr);
    return kOfxStatOK;
}
FakeHost::FakeHost() {
    current = this;
    props.propSetString = [](OfxPropertySetHandle p, const char* k, int i, const char* v) -> OfxStatus { put(properties(p).strings, k, i, std::string(v)); return kOfxStatOK; };
    props.propSetPointer = [](OfxPropertySetHandle p, const char* k, int, void* v) -> OfxStatus { properties(p).pointers[k] = v; return kOfxStatOK; };
    props.propSetInt = [](OfxPropertySetHandle p, const char* k, int i, int v) -> OfxStatus { put(properties(p).ints, k, i, v); return kOfxStatOK; };
    props.propSetStringN = [](OfxPropertySetHandle p, const char* k, int n, const char* const* v) -> OfxStatus { for (int i=0; i<n; ++i) put(properties(p).strings, k, i, std::string(v[i])); return kOfxStatOK; };
    props.propSetDouble = [](OfxPropertySetHandle p, const char* k, int i, double v) -> OfxStatus { put(properties(p).doubles, k, i, v); return kOfxStatOK; };
    props.propSetDoubleN = [](OfxPropertySetHandle p, const char* k, int n, const double* v) -> OfxStatus { for (int i=0; i<n; ++i) put(properties(p).doubles, k, i, v[i]); return kOfxStatOK; };
    props.propGetString = [](OfxPropertySetHandle p, const char* k, int i, char** v) -> OfxStatus {
        auto it = properties(p).strings.find(k);
        if (it == properties(p).strings.end() || i < 0 || static_cast<int>(it->second.size()) <= i) return kOfxStatErrUnknown;
        *v = const_cast<char*>(it->second[i].c_str()); return kOfxStatOK;
    };
    props.propGetInt = [](OfxPropertySetHandle p, const char* k, int i, int* v) -> OfxStatus { return get(properties(p).ints, k, i, v); };
    props.propGetDouble = [](OfxPropertySetHandle p, const char* k, int i, double* v) -> OfxStatus { return get(properties(p).doubles, k, i, v); };
    props.propGetPointer = [](OfxPropertySetHandle p, const char* k, int, void** v) -> OfxStatus {
        auto it = properties(p).pointers.find(k); if (it == properties(p).pointers.end()) return kOfxStatErrUnknown;
        *v = it->second; return kOfxStatOK;
    };
    props.propGetIntN = [](OfxPropertySetHandle p, const char* k, int n, int* v) -> OfxStatus {
        for (int i = 0; i < n; ++i) if (get(properties(p).ints, k, i, &v[i]) != kOfxStatOK) return kOfxStatErrUnknown;
        return kOfxStatOK;
    };
    props.propGetDoubleN = [](OfxPropertySetHandle p, const char* k, int n, double* v) -> OfxStatus {
        for (int i = 0; i < n; ++i) if (get(properties(p).doubles, k, i, &v[i]) != kOfxStatOK) return kOfxStatErrUnknown;
        return kOfxStatOK;
    };
    props.propGetDimension = [](OfxPropertySetHandle p, const char* k, int* n) -> OfxStatus {
        auto it = properties(p).strings.find(k); if (it == properties(p).strings.end()) return kOfxStatErrUnknown;
        *n = static_cast<int>(it->second.size()); return kOfxStatOK;
    };
    fx.getPropertySet = [](OfxImageEffectHandle, OfxPropertySetHandle* p) -> OfxStatus { *p = handle(current->effectProps); return kOfxStatOK; };
    fx.getParamSet = [](OfxImageEffectHandle, OfxParamSetHandle* p) -> OfxStatus { *p = current->paramSet(); return kOfxStatOK; };
    fx.clipGetHandle = [](OfxImageEffectHandle, const char* name, OfxImageClipHandle* p, OfxPropertySetHandle* propsOut) -> OfxStatus {
        *p = std::strcmp(name, kOfxImageEffectOutputClipName) == 0 ? current->dst() : current->src();
        if (propsOut) *propsOut = nullptr;
        return kOfxStatOK;
    };
    fx.clipGetRegionOfDefinition = [](OfxImageClipHandle, OfxTime, OfxRectD* r) -> OfxStatus { *r={0,0,640,480}; return kOfxStatOK; };
    fx.clipGetImage = [](OfxImageClipHandle c, OfxTime, const OfxRectD*, OfxPropertySetHandle* out) -> OfxStatus {
        if (c == current->src() && !current->sourceAvailable) return kOfxStatFailed;
        *out = handle(c == current->src() ? current->sourceImage : current->outputImage); return kOfxStatOK;
    };
    fx.clipReleaseImage = [](OfxPropertySetHandle) -> OfxStatus { return kOfxStatOK; };
    fx.abort = [](OfxImageEffectHandle) -> int { return 0; };
    messages.message = captureMessage;
    interact.interactGetPropertySet = [](OfxInteractHandle h, OfxPropertySetHandle* p) -> OfxStatus { *p = reinterpret_cast<OfxPropertySetHandle>(h); return kOfxStatOK; };
    interact.interactRedraw = [](OfxInteractHandle) -> OfxStatus { ++current->redraws; return kOfxStatOK; };
    params.paramDefine = [](OfxParamSetHandle, const char*, const char* name, OfxPropertySetHandle* out) -> OfxStatus { *out = handle(current->definitions[name]); return kOfxStatOK; };
    params.paramGetHandle = [](OfxParamSetHandle, const char* name, OfxParamHandle* out, OfxPropertySetHandle*) -> OfxStatus {
        if (std::strcmp(name, pname::kStatusText) && std::strcmp(name, pname::kLogPath) &&
            std::strcmp(name, pname::kDetailRefinement) && std::strcmp(name, pname::kEdgeFeather) &&
            std::strcmp(name, pname::kEdgeExpansion) && std::strcmp(name, pname::kEdgeSmoothing) &&
            std::strcmp(name, pname::kShiftMatte) && std::strcmp(name, pname::kFallbackKeyTolerance) &&
            std::strcmp(name, pname::kFallbackKeySoftness) && std::strcmp(name, pname::kModel) &&
            std::strcmp(name, pname::kGuidancePath) && std::strcmp(name, pname::kBrushData) &&
            std::strcmp(name, pname::kBrushMode) && std::strcmp(name, pname::kBrushRadius) && std::strcmp(name, pname::kSelectionMode) && std::strcmp(name, pname::kGuidedBrushBehavior)) return kOfxStatErrUnknown;
        *out = reinterpret_cast<OfxParamHandle>(const_cast<char*>(name)); return kOfxStatOK;
    };
    params.paramGetValueAtTime = getParameterAtTime;
    params.paramSetValue = setParameter;
    // Simulate a host which synchronously notifies on every plugin-authored parameter write.
    params.paramSetValueAtTime = setParameterAtTime;
    host.host = handle(hostProps); host.fetchSuite = fetch;
    for (Properties* p : {&sourceImage, &outputImage}) {
        put(p->strings, kOfxImageEffectPropComponents, 0, std::string(kOfxImageComponentRGBA));
        put(p->strings, kOfxImageEffectPropPixelDepth, 0, std::string(kOfxBitDepthFloat));
        put(p->strings, kOfxImageEffectPropPreMultiplication, 0, std::string(kOfxImageUnPreMultiplied));
        p->ints[kOfxImagePropBounds] = {0, 0, 64, 64};
        p->ints[kOfxImagePropRowBytes] = {64 * 4 * static_cast<int>(sizeof(float))};
    }
    sourceImage.pointers[kOfxImagePropData] = source.data();
    outputImage.pointers[kOfxImagePropData] = output.data();
    args.ints[kOfxImageEffectPropRenderWindow] = {0, 0, 64, 64};
    aibgr::Log::instance().setFilePath("");
    HostBridge::instance().storeHost(&host);
    HostBridge::instance().acquireSuites();
}
} // namespace

TEST(ofxStability, descriptorLimitsConcurrentRenders) {
    FakeHost host;
    CHECK_EQ(describeEffect(host.effect()), kOfxStatOK);
    const auto safety = host.effectProps.strings.find(kOfxImageEffectPluginRenderThreadSafety);
    CHECK(safety != host.effectProps.strings.end());
    if (safety != host.effectProps.strings.end() && !safety->second.empty()) {
        CHECK_EQ(safety->second[0], std::string(kOfxImageEffectRenderInstanceSafe));
    }
    const auto threading = host.effectProps.ints.find(kOfxImageEffectPluginPropHostFrameThreading);
    CHECK(threading != host.effectProps.ints.end());
    if (threading != host.effectProps.ints.end() && !threading->second.empty()) {
        CHECK_EQ(threading->second[0], 0);
    }
}

TEST(ofxStability, changingSettingsDoesNotWriteDiagnosticParameters) {
    FakeHost host;
    CHECK_EQ(pluginMain(kOfxActionCreateInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
    put(host.args.strings, kOfxPropName, 0, std::string(pname::kQuality));
    CHECK_EQ(pluginMain(kOfxActionInstanceChanged, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    CHECK_EQ(host.paramWrites, 0);
    put(host.args.strings, kOfxPropName, 0, std::string(pname::kStatusText));
    CHECK_EQ(pluginMain(kOfxActionInstanceChanged, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    CHECK_EQ(host.paramWrites, 0);
    CHECK_EQ(pluginMain(kOfxActionDestroyInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
}

TEST(ofxStability, renderAndPurgeNeverWriteDiagnosticParameters) {
    FakeHost host;
    CHECK_EQ(pluginMain(kOfxActionCreateInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    CHECK_EQ(host.paramWrites, 0);
    host.sourceAvailable = false;
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    CHECK_EQ(host.paramWrites, 0);
    CHECK_EQ(pluginMain(kOfxActionPurgeCaches, host.effect(), nullptr, nullptr), kOfxStatOK);
    CHECK_EQ(host.paramWrites, 0);
    CHECK_EQ(pluginMain(kOfxActionDestroyInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
}

TEST(ofxStability, resetButtonNotifiesWithoutRecursiveStatusWrites) {
    FakeHost host;
    CHECK_EQ(pluginMain(kOfxActionCreateInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
    put(host.args.strings, kOfxPropName, 0, std::string(pname::kResetRefine));
    CHECK_EQ(pluginMain(kOfxActionInstanceChanged, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    CHECK_EQ(host.paramWrites, 5);
    CHECK_EQ(pluginMain(kOfxActionDestroyInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
}

TEST(ofxStability, concurrentRenderAndPurgeProtectScratch) {
    FakeHost host;
    CHECK_EQ(pluginMain(kOfxActionCreateInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
    std::atomic<int> failures{0};
    auto render = [&] {
        for (int i = 0; i < 30; ++i) {
            if (pluginMain(kOfxImageEffectActionRender, host.effect(), handle(host.args), nullptr) != kOfxStatOK) ++failures;
        }
    };
    auto purge = [&] {
        for (int i = 0; i < 30; ++i) {
            if (pluginMain(kOfxActionPurgeCaches, host.effect(), nullptr, nullptr) != kOfxStatOK) ++failures;
        }
    };
    std::thread first(render), second(render), third(purge);
    first.join(); second.join(); third.join();
    CHECK_EQ(failures.load(), 0);
    CHECK_EQ(host.paramWrites, 0);
    CHECK_EQ(pluginMain(kOfxActionDestroyInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
}

TEST(ofxStability, colourKeySettingsAreReadAndChangeTheRenderedMatte) {
    FakeHost host;
    // Green border, orange centre: a predictable simple key.
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
        float* p = &host.source[(y * 64 + x) * 4];
        p[0] = (x >= 16 && x < 48 && y >= 16 && y < 48) ? 0.6f : 0.1f;
        p[1] = 0.5f; p[2] = 0.1f; p[3] = 1.0f;
    }
    host.tolerance = 0.05; host.softness = 0.02;
    aibgr::PipelineSettings settings;
    std::string note;
    CHECK(readSettings(host.paramSet(), 0.0, settings, note).ok());
    CHECK_NEAR(settings.fallbackKeyTolerance, 0.05, 1e-6);
    CHECK_NEAR(settings.fallbackKeySoftness, 0.02, 1e-6);
    CHECK_EQ(pluginMain(kOfxActionCreateInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    const float lowToleranceAlpha = host.output[(32 * 64 + 32) * 4 + 3];
    host.tolerance = 0.9;
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    const float highToleranceAlpha = host.output[(32 * 64 + 32) * 4 + 3];
    CHECK(lowToleranceAlpha > highToleranceAlpha + 0.2f);
    host.tolerance = 0.3; host.softness = 0.02;
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    const float narrowAlpha = host.output[(32 * 64 + 32) * 4 + 3];
    host.softness = 0.3;
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    const float wideAlpha = host.output[(32 * 64 + 32) * 4 + 3];
    CHECK_MSG(std::fabs(narrowAlpha - wideAlpha) > 0.01f, std::to_string(narrowAlpha) + " vs " + std::to_string(wideAlpha));
    CHECK_EQ(pluginMain(kOfxActionDestroyInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
}

TEST(ofxStability, choosesWritableLogAndFallsBackWithoutTruncating) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("aibgr-log-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    const std::string preferred = (dir / "preferred.log").string();
    const std::string fallback = (dir / "fallback.log").string();
    CHECK_EQ(selectWritableLogPath(preferred, fallback), preferred);
    CHECK(fs::exists(preferred));
    CHECK(!fs::exists(fallback));
    const std::string unavailable = (dir / "missing" / "log.txt").string();
    CHECK_EQ(selectWritableLogPath(unavailable, fallback), fallback);
    CHECK(fs::exists(fallback));
    CHECK(selectWritableLogPath(unavailable, (dir / "missing2" / "log.txt").string()).empty());
    fs::remove_all(dir);
}

TEST(ofxStability, realModelCanBeSelectedWhenAssetsSupplied) {
    if (!std::getenv("AIBGR_TEST_MODEL") || !aibgr::fileExists(aiRuntimePath()) || !aibgr::fileExists(aiModelPath())) return;
    FakeHost host;
    setModelUsesStrChoice(false);
    host.modelIndex = 1;
    host.guidancePath = std::string(AIBGR_TEST_DATA_DIR) + "/guidance.png";
    CHECK_EQ(pluginMain(kOfxActionCreateInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    CHECK_NEAR(host.output[(54*64+10)*4+3], 1.0, 1e-6);
    CHECK_NEAR(host.output[(54*64+54)*4+3], 0.0, 1e-6);
    PluginInstance* instance = findInstance(host.effect());
    CHECK(instance != nullptr);
    if (instance) {
        CHECK(instance->pipeline().matteSource() != nullptr);
        if (instance->pipeline().matteSource()) CHECK_EQ(std::string(instance->pipeline().matteSource()->name()), std::string("U2NetP CPU"));
    }
    host.brushData = "AIBGR1 1 R 5 1 10 54 ";
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    CHECK_NEAR(host.output[(54*64+10)*4+3], 0.0, 1e-6); // brush overrides PNG + AI
    CHECK_EQ(pluginMain(kOfxActionDestroyInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
}

#if defined(kOfxParamTypeStrChoice)
TEST(ofxStability, strChoiceHasStableModelValues) {
    FakeHost host;
    put(host.hostProps.ints, kOfxParamHostPropSupportsStrChoice, 0, 1);
    CHECK(HostBridge::instance().acquireSuites().ok());
    CHECK(defineParameters(host.paramSet(), {{"u2netp", "U2NetP (AI, CPU)"}}).ok());
    const auto& p = host.definitions[pname::kModel].strings;
    CHECK_EQ(p.at(kOfxParamPropChoiceEnum)[0], std::string("__fallback__"));
    CHECK_EQ(p.at(kOfxParamPropChoiceEnum)[1], std::string("u2netp"));
    CHECK_EQ(p.at(kOfxParamPropDefault)[0], std::string("u2netp"));
    host.modelValue = "u2netp";
    aibgr::PipelineSettings settings;
    std::string note;
    CHECK(readSettings(host.paramSet(), 0.0, settings, note).ok());
    CHECK(settings.model == aibgr::ModelId::SmallMatte);
    setModelUsesStrChoice(false);
}
#endif

TEST(ofxStability, guidanceFileCorrectsMatteAndReloadsWithoutParamWrites) {
    FakeHost host;
    setModelUsesStrChoice(false);
    host.guidancePath = std::string(AIBGR_TEST_DATA_DIR) + "/guidance.png";
    CHECK_EQ(pluginMain(kOfxActionCreateInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    CHECK_NEAR(host.output[(54*64+10)*4+3], 1.0, 1e-6);
    CHECK_NEAR(host.output[(54*64+54)*4+3], 0.0, 1e-6);
    CHECK_EQ(host.paramWrites, 0);
    put(host.args.strings, kOfxPropName, 0, std::string(pname::kReloadGuidance));
    CHECK_EQ(pluginMain(kOfxActionInstanceChanged, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    CHECK_NEAR(host.output[(54*64+10)*4+3], 1.0, 1e-6);
    CHECK_EQ(host.paramWrites, 0);
    CHECK_EQ(pluginMain(kOfxActionDestroyInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
}

TEST(ofxStability, viewerBrushPenActionsPersistAndRender) {
    FakeHost host; setModelUsesStrChoice(false);
    put(host.hostProps.ints, kOfxImageEffectPropSupportsOverlays, 0, 1);
    CHECK(HostBridge::instance().acquireSuites().ok());
    CHECK_EQ(describeEffect(host.effect()), kOfxStatOK);
    CHECK(host.effectProps.pointers[kOfxImageEffectPluginPropOverlayInteractV1] != nullptr);
    CHECK_EQ(pluginMain(kOfxActionCreateInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
    Properties overlay, pen;
    overlay.pointers[kOfxPropEffectInstance] = host.effect();
    pen.doubles[kOfxInteractPropPenPosition] = {20.0, 20.0};
    const void* h = reinterpret_cast<const void*>(handle(overlay));
    CHECK_EQ(brushOverlayMain(kOfxActionCreateInstance, h, nullptr, nullptr), kOfxStatOK);
    CHECK_EQ(brushOverlayMain(kOfxInteractActionDraw, h, handle(pen), nullptr), kOfxStatOK);
    CHECK_EQ(brushOverlayMain(kOfxInteractActionPenDown, h, handle(pen), nullptr), kOfxStatReplyDefault);
    host.brushMode = 1;
    CHECK_EQ(brushOverlayMain(kOfxInteractActionPenDown, h, handle(pen), nullptr), kOfxStatOK);
    pen.doubles[kOfxInteractPropPenPosition] = {30.0, 20.0};
    CHECK_EQ(brushOverlayMain(kOfxInteractActionPenMotion, h, handle(pen), nullptr), kOfxStatOK);
    CHECK_EQ(brushOverlayMain(kOfxInteractActionPenUp, h, handle(pen), nullptr), kOfxStatOK);
    CHECK_EQ(host.paramWrites, 1);
    CHECK(host.redraws > 0);
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    CHECK_NEAR(host.output[(20*64+25)*4+3], 1.0, 1e-6);
    CHECK_NEAR(host.output[(50*64+50)*4+3], 0.0, 1e-6);
    CHECK_EQ(host.paramWrites, 1); // renders do not write back to parameters
    // Host-saved parameter data is enough for a new effect instance to reproduce the stroke.
    CHECK_EQ(pluginMain(kOfxActionDestroyInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
    CHECK_EQ(pluginMain(kOfxActionCreateInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    CHECK_NEAR(host.output[(20*64+25)*4+3], 1.0, 1e-6);
    put(host.args.strings, kOfxPropName, 0, std::string(pname::kBrushUndo));
    CHECK_EQ(pluginMain(kOfxActionInstanceChanged, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender, host.effect(), handle(host.args), nullptr), kOfxStatOK);
    CHECK_NEAR(host.output[(20*64+25)*4+3], 0.0, 1e-6);
    CHECK_EQ(brushOverlayMain(kOfxActionDestroyInstance, h, nullptr, nullptr), kOfxStatOK);
    CHECK_EQ(pluginMain(kOfxActionDestroyInstance, host.effect(), nullptr, nullptr), kOfxStatOK);
}

TEST(ofxStability, toolbarSelectsOutlineAndConsumesClick) {
    FakeHost host; setModelUsesStrChoice(false);
    CHECK_EQ(pluginMain(kOfxActionCreateInstance,host.effect(),nullptr,nullptr),kOfxStatOK);
    Properties overlay, pen; overlay.pointers[kOfxPropEffectInstance]=host.effect();
    pen.doubles[kOfxInteractPropPixelScale]={1,1};
    pen.doubles[kOfxInteractPropPenPosition]={55,20}; // L button
    const void* h=reinterpret_cast<const void*>(handle(overlay));
    CHECK_EQ(brushOverlayMain(kOfxActionCreateInstance,h,nullptr,nullptr),kOfxStatOK);
    CHECK_EQ(brushOverlayMain(kOfxInteractActionPenDown,h,handle(pen),nullptr),kOfxStatOK);
    CHECK_EQ(brushOverlayMain(kOfxInteractActionPenUp,h,handle(pen),nullptr),kOfxStatOK);
    CHECK_EQ(host.brushMode,3); CHECK_EQ(host.selectionMode,1); CHECK(host.brushData.empty());
    pen.doubles[kOfxInteractPropPenPosition]={300,100};
    CHECK_EQ(brushOverlayMain(kOfxInteractActionPenDown,h,handle(pen),nullptr),kOfxStatOK);
    pen.doubles[kOfxInteractPropPenPosition]={400,100};
    CHECK_EQ(brushOverlayMain(kOfxInteractActionPenMotion,h,handle(pen),nullptr),kOfxStatOK);
    pen.doubles[kOfxInteractPropPenPosition]={400,200};
    CHECK_EQ(brushOverlayMain(kOfxInteractActionPenUp,h,handle(pen),nullptr),kOfxStatOK);
    std::vector<aibgr::BrushStroke> strokes; CHECK(aibgr::parseBrushStrokes(host.brushData,strokes));
    CHECK_EQ(strokes.size(),size_t(1)); CHECK(strokes[0].outline);
    CHECK_EQ(brushOverlayMain(kOfxActionDestroyInstance,h,nullptr,nullptr),kOfxStatOK);
    CHECK_EQ(pluginMain(kOfxActionDestroyInstance,host.effect(),nullptr,nullptr),kOfxStatOK);
}

TEST(ofxStability, guidedModeRendersUnmarkedRegionsAndPersists) {
    FakeHost host; setModelUsesStrChoice(false);
    for (int y=0;y<64;++y) for(int x=0;x<64;++x) {
        const size_t i=(y*64+x)*4; const bool subject=x>=16 && x<48 && y>=16 && y<48;
        host.source[i]=subject ? 0.8f : 0.1f; host.source[i+1]=subject ? 0.1f : 0.2f;
        host.source[i+2]=subject ? 0.1f : 0.9f; host.source[i+3]=1;
    }
    host.brushData=aibgr::serialiseBrushStrokes({{true,3,{{12,12},{52,12},{52,52},{12,52}},true}, {true,3,{{20,20}}}});
    host.selectionMode=1;
    CHECK_EQ(pluginMain(kOfxActionCreateInstance,host.effect(),nullptr,nullptr),kOfxStatOK);
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender,host.effect(),handle(host.args),nullptr),kOfxStatOK);
    CHECK(host.output[(40*64+40)*4+3]>0.7f);
    CHECK(host.output[(5*64+5)*4+3]<0.01f);
    CHECK_EQ(host.paramWrites,0);
    CHECK_EQ(pluginMain(kOfxActionDestroyInstance,host.effect(),nullptr,nullptr),kOfxStatOK);
    CHECK_EQ(pluginMain(kOfxActionCreateInstance,host.effect(),nullptr,nullptr),kOfxStatOK);
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender,host.effect(),handle(host.args),nullptr),kOfxStatOK);
    CHECK(host.output[(40*64+40)*4+3]>0.7f);
    CHECK_EQ(pluginMain(kOfxActionDestroyInstance,host.effect(),nullptr,nullptr),kOfxStatOK);
}

TEST(ofxStability, pointLimitWarnsWithoutDiscardingSavedData) {
    FakeHost host; setModelUsesStrChoice(false);
    aibgr::BrushStroke full; full.keep=true; full.radius=3;
    for(size_t i=0;i<aibgr::kMaxPointsPerStroke;++i) full.points.push_back({double(i),50.0});
    host.brushData=aibgr::serialiseBrushStrokes({full,full}); const std::string original=host.brushData;
    host.brushMode=1;
    CHECK_EQ(pluginMain(kOfxActionCreateInstance,host.effect(),nullptr,nullptr),kOfxStatOK);
    Properties overlay, pen; overlay.pointers[kOfxPropEffectInstance]=host.effect();
    pen.doubles[kOfxInteractPropPenPosition]={400,200};
    const void* h=reinterpret_cast<const void*>(handle(overlay));
    CHECK_EQ(brushOverlayMain(kOfxActionCreateInstance,h,nullptr,nullptr),kOfxStatOK);
    CHECK_EQ(brushOverlayMain(kOfxInteractActionPenDown,h,handle(pen),nullptr),kOfxStatOK);
    CHECK_EQ(brushOverlayMain(kOfxInteractActionPenUp,h,handle(pen),nullptr),kOfxStatOK);
    CHECK(host.warning.find("limit reached")!=std::string::npos);
    CHECK_EQ(host.brushData,original); CHECK_EQ(host.paramWrites,0);
    CHECK_EQ(brushOverlayMain(kOfxActionDestroyInstance,h,nullptr,nullptr),kOfxStatOK);
    CHECK_EQ(pluginMain(kOfxActionDestroyInstance,host.effect(),nullptr,nullptr),kOfxStatOK);
}

TEST(ofxStability, longKeepAndRemoveStrokesContinuePastOldLimit) {
    FakeHost host; setModelUsesStrChoice(false); host.brushRadius=3;
    CHECK_EQ(pluginMain(kOfxActionCreateInstance,host.effect(),nullptr,nullptr),kOfxStatOK);
    Properties overlay, pen; overlay.pointers[kOfxPropEffectInstance]=host.effect();
    const void* h=reinterpret_cast<const void*>(handle(overlay));
    CHECK_EQ(brushOverlayMain(kOfxActionCreateInstance,h,nullptr,nullptr),kOfxStatOK);
    for(int stroke=0;stroke<3;++stroke) {
        host.brushMode=stroke==2 ? 2 : 1;
        pen.doubles[kOfxInteractPropPenPosition]={400.0,100.0+stroke*20};
        CHECK_EQ(brushOverlayMain(kOfxInteractActionPenDown,h,handle(pen),nullptr),kOfxStatOK);
        for(int i=1;i<=300;++i) {
            pen.doubles[kOfxInteractPropPenPosition]={400.0+i*2,100.0+stroke*20+(i%2)*4};
            CHECK_EQ(brushOverlayMain(kOfxInteractActionPenMotion,h,handle(pen),nullptr),kOfxStatOK);
        }
        CHECK_EQ(brushOverlayMain(kOfxInteractActionPenUp,h,handle(pen),nullptr),kOfxStatOK);
    }
    std::vector<aibgr::BrushStroke> strokes;
    CHECK(aibgr::parseBrushStrokes(host.brushData,strokes)); CHECK_EQ(strokes.size(),size_t(3));
    CHECK(host.brushData.find("AIBGR3 ")==0); CHECK(host.warning.empty());
    CHECK(strokes[0].points.size()+strokes[1].points.size()>512); CHECK(!strokes[2].keep);
    CHECK_EQ(brushOverlayMain(kOfxActionDestroyInstance,h,nullptr,nullptr),kOfxStatOK);
    CHECK_EQ(pluginMain(kOfxActionDestroyInstance,host.effect(),nullptr,nullptr),kOfxStatOK);
}

TEST(ofxStability, guidedSamplesOnlyDoesNotApplyDirectPaintWithoutBothClasses) {
    FakeHost host; setModelUsesStrChoice(false); host.selectionMode=1;
    host.brushData=aibgr::serialiseBrushStrokes({{true,6,{{20,20}}}});
    CHECK_EQ(pluginMain(kOfxActionCreateInstance,host.effect(),nullptr,nullptr),kOfxStatOK);
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender,host.effect(),handle(host.args),nullptr),kOfxStatOK);
    CHECK_NEAR(host.output[(20*64+20)*4+3],1.0,1e-6); // legacy/default direct paint
    host.brushBehavior=1;
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender,host.effect(),handle(host.args),nullptr),kOfxStatOK);
    CHECK_NEAR(host.output[(20*64+20)*4+3],0.0,1e-6); // one seed class: prior only
    host.selectionMode=0; // manual mode always uses paint
    CHECK_EQ(pluginMain(kOfxImageEffectActionRender,host.effect(),handle(host.args),nullptr),kOfxStatOK);
    CHECK_NEAR(host.output[(20*64+20)*4+3],1.0,1e-6);
    CHECK_EQ(pluginMain(kOfxActionDestroyInstance,host.effect(),nullptr,nullptr),kOfxStatOK);
}

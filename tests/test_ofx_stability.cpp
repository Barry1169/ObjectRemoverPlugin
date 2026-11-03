// SPDX-License-Identifier: MIT
// Small fake OFX host: exercises the real action dispatcher, not just the pipeline core.
#include "TestCase.h"
#include "plugin/BgRemoverPlugin.h"
#include "plugin/HostBridge.h"
#include "plugin/ParamDefs.h"
#include "util/Log.h"

#include <atomic>
#include <cstring>
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
    OfxPropertySuiteV1 props{};
    OfxImageEffectSuiteV1 fx{};
    OfxParameterSuiteV1 params{};
    OfxHost host{};
    std::vector<float> source = std::vector<float>(64 * 64 * 4, 0.5f);
    std::vector<float> output = std::vector<float>(64 * 64 * 4, 0.0f);
    int effectToken = 0, paramToken = 0, srcToken = 0, dstToken = 0;
    int paramWrites = 0, recursionDepth = 0;
    bool sourceAvailable = true;
    OfxImageEffectHandle effect() { return reinterpret_cast<OfxImageEffectHandle>(&effectToken); }
    OfxParamSetHandle paramSet() { return reinterpret_cast<OfxParamSetHandle>(&paramToken); }
    OfxImageClipHandle src() { return reinterpret_cast<OfxImageClipHandle>(&srcToken); }
    OfxImageClipHandle dst() { return reinterpret_cast<OfxImageClipHandle>(&dstToken); }
    FakeHost();
};
FakeHost* current = nullptr;

const void* fetch(OfxPropertySetHandle, const char* name, int version) {
    if (version != 1) return nullptr;
    if (std::strcmp(name, kOfxPropertySuite) == 0) return &current->props;
    if (std::strcmp(name, kOfxImageEffectSuite) == 0) return &current->fx;
    if (std::strcmp(name, kOfxParameterSuite) == 0) return &current->params;
    return nullptr;
}
OfxStatus getParameterAtTime(OfxParamHandle, OfxTime, ...) { return kOfxStatErrUnknown; }
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
FakeHost::FakeHost() {
    current = this;
    props.propSetString = [](OfxPropertySetHandle p, const char* k, int i, const char* v) -> OfxStatus { put(properties(p).strings, k, i, std::string(v)); return kOfxStatOK; };
    props.propSetInt = [](OfxPropertySetHandle p, const char* k, int i, int v) -> OfxStatus { put(properties(p).ints, k, i, v); return kOfxStatOK; };
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
    fx.clipGetImage = [](OfxImageClipHandle c, OfxTime, const OfxRectD*, OfxPropertySetHandle* out) -> OfxStatus {
        if (c == current->src() && !current->sourceAvailable) return kOfxStatFailed;
        *out = handle(c == current->src() ? current->sourceImage : current->outputImage); return kOfxStatOK;
    };
    fx.clipReleaseImage = [](OfxPropertySetHandle) -> OfxStatus { return kOfxStatOK; };
    fx.abort = [](OfxImageEffectHandle) -> int { return 0; };
    params.paramGetHandle = [](OfxParamSetHandle, const char* name, OfxParamHandle* out, OfxPropertySetHandle*) -> OfxStatus {
        if (std::strcmp(name, pname::kStatusText) && std::strcmp(name, pname::kLogPath) &&
            std::strcmp(name, pname::kDetailRefinement) && std::strcmp(name, pname::kEdgeFeather) &&
            std::strcmp(name, pname::kEdgeExpansion) && std::strcmp(name, pname::kEdgeSmoothing) &&
            std::strcmp(name, pname::kShiftMatte)) return kOfxStatErrUnknown;
        *out = reinterpret_cast<OfxParamHandle>(const_cast<char*>(name)); return kOfxStatOK;
    };
    params.paramGetValueAtTime = getParameterAtTime;
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

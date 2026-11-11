// SPDX-License-Identifier: MIT
#include "inference/U2NetSegmenter.h"
#include "matte/Resize.h"
#include "util/Paths.h"
#include <onnxruntime_c_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace aibgr {
namespace {
constexpr int kSize = 320;
constexpr size_t kPixels = kSize * kSize;
using GetApiBaseFn = const OrtApiBase* (ORT_API_CALL*)();

struct OrtValueGuard {
    const OrtApi* api;
    OrtValue* value = nullptr;
    ~OrtValueGuard() { if (value) api->ReleaseValue(value); }
};
}

struct U2NetSegmenter::Impl {
#if defined(_WIN32)
    HMODULE library = nullptr;
#else
    void* library = nullptr;
#endif
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtSessionOptions* options = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* memory = nullptr;
    std::string inputName, outputName;
    FloatImage scaled;
    Matte raw;
    std::vector<float> tensor;

    ~Impl() {
        if (api) {
            if (memory) api->ReleaseMemoryInfo(memory);
            if (session) api->ReleaseSession(session);
            if (options) api->ReleaseSessionOptions(options);
            if (env) api->ReleaseEnv(env);
        }
#if defined(_WIN32)
        if (library) FreeLibrary(library);
#else
        if (library) dlclose(library);
#endif
    }
    void check(OrtStatus* s) {
        if (!s) return;
        const std::string text = api->GetErrorMessage(s);
        api->ReleaseStatus(s);
        throw std::runtime_error(text);
    }
    void validateInput() {
        OrtTypeInfo* type = nullptr;
        check(api->SessionGetInputTypeInfo(session, 0, &type));
        try {
            const OrtTensorTypeAndShapeInfo* shape = nullptr;
            check(api->CastTypeInfoToTensorInfo(type, &shape));
            if (!shape) throw std::runtime_error("model input is not a tensor");
            ONNXTensorElementDataType element;
            size_t count = 0;
            check(api->GetTensorElementType(shape, &element));
            check(api->GetDimensionsCount(shape, &count));
            if (element != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || count != 4)
                throw std::runtime_error("model must take a float NCHW tensor");
            std::array<int64_t, 4> dims{};
            check(api->GetDimensions(shape, dims.data(), dims.size()));
            if (dims != std::array<int64_t, 4>{1, 3, kSize, kSize})
                throw std::runtime_error("expected fixed input shape [1,3,320,320]");
        } catch (...) { api->ReleaseTypeInfo(type); throw; }
        api->ReleaseTypeInfo(type);
    }
    void load(const std::string& runtime, const std::string& model) {
#if defined(_WIN32)
        const auto libPath = widen(runtime);
        library = libPath.empty() ? nullptr : LoadLibraryExW(libPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!library) throw std::runtime_error("cannot load ONNX Runtime DLL or its dependencies");
        auto getBase = reinterpret_cast<GetApiBaseFn>(GetProcAddress(library, "OrtGetApiBase"));
#else
        library = dlopen(runtime.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!library) throw std::runtime_error("cannot load ONNX Runtime shared library or its dependencies");
        auto getBase = reinterpret_cast<GetApiBaseFn>(dlsym(library, "OrtGetApiBase"));
#endif
        if (!getBase) throw std::runtime_error("ONNX Runtime is missing OrtGetApiBase");
        const OrtApiBase* base = getBase();
        api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
        if (!api) throw std::runtime_error("ONNX Runtime API version 16 is unavailable (requires 1.16+)");
        check(api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "aibgr", &env));
        check(api->CreateSessionOptions(&options));
        check(api->SetIntraOpNumThreads(options, 2));
#if defined(_WIN32)
        const auto modelWide = widen(model);
        if (modelWide.empty()) throw std::runtime_error("invalid model path");
        check(api->CreateSession(env, modelWide.c_str(), options, &session));
#else
        check(api->CreateSession(env, model.c_str(), options, &session));
#endif
        size_t inputs = 0, outputs = 0;
        check(api->SessionGetInputCount(session, &inputs));
        check(api->SessionGetOutputCount(session, &outputs));
        if (inputs != 1 || outputs == 0) throw std::runtime_error("model must have one input and at least one output");
        validateInput();
        OrtAllocator* allocator = nullptr;
        check(api->GetAllocatorWithDefaultOptions(&allocator));
        char* name = nullptr;
        check(api->SessionGetInputName(session, 0, allocator, &name));
        inputName = name ? name : "";
        if (name) check(api->AllocatorFree(allocator, name));
        name = nullptr;
        check(api->SessionGetOutputName(session, 0, allocator, &name));
        outputName = name ? name : "";
        if (name) check(api->AllocatorFree(allocator, name));
        if (inputName.empty() || outputName.empty()) throw std::runtime_error("model input/output name missing");
        check(api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory));
        tensor.resize(kPixels * 3);
    }
    void infer(const FloatImage& image, Matte& out) {
        scaled.resize(kSize, kSize);
        resizeArea(image, scaled);
        float maxValue = 0.0f;
        for (size_t i = 0; i < kPixels; ++i) for (int c = 0; c < 3; ++c) {
            float v = scaled.data()[i * 4 + c];
            if (std::isfinite(v)) maxValue = std::max(maxValue, std::clamp(v, 0.0f, 1.0f));
        }
        maxValue = std::max(maxValue, 1e-6f);
        constexpr float means[] = {0.485f, 0.456f, 0.406f};
        constexpr float stds[] = {0.229f, 0.224f, 0.225f};
        for (size_t i = 0; i < kPixels; ++i) for (int c = 0; c < 3; ++c) {
            // OFX pixel coordinates increase bottom-to-top; image models expect top-to-bottom.
            const size_t source = (kSize - 1 - i / kSize) * kSize + i % kSize;
            float v = scaled.data()[source * 4 + c];
            v = std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
            tensor[c * kPixels + i] = (v / maxValue - means[c]) / stds[c];
        }
        OrtValueGuard input{api}, output{api};
        const int64_t dims[] = {1, 3, kSize, kSize};
        check(api->CreateTensorWithDataAsOrtValue(memory, tensor.data(), tensor.size() * sizeof(float),
                                                  dims, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input.value));
        const char* inName = inputName.c_str();
        const char* outName = outputName.c_str();
        const OrtValue* inValue = input.value;
        check(api->Run(session, nullptr, &inName, &inValue, 1, &outName, 1, &output.value));
        OrtTensorTypeAndShapeInfo* info = nullptr;
        check(api->GetTensorTypeAndShape(output.value, &info));
        try {
            size_t count = 0; ONNXTensorElementDataType type;
            check(api->GetTensorShapeElementCount(info, &count));
            check(api->GetTensorElementType(info, &type));
            size_t rank = 0; check(api->GetDimensionsCount(info, &rank));
            if (type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || rank != 4 || count != kPixels)
                throw std::runtime_error("model output must be float [1,1,320,320]");
            std::array<int64_t, 4> shape{}; check(api->GetDimensions(info, shape.data(), shape.size()));
            if (shape != std::array<int64_t, 4>{1, 1, kSize, kSize})
                throw std::runtime_error("model output must be float [1,1,320,320]");
        } catch (...) { api->ReleaseTensorTypeAndShapeInfo(info); throw; }
        api->ReleaseTensorTypeAndShapeInfo(info);
        float* data = nullptr;
        check(api->GetTensorMutableData(output.value, reinterpret_cast<void**>(&data)));
        if (!data) throw std::runtime_error("model output is null");
        float lo = std::numeric_limits<float>::infinity(), hi = -lo;
        for (size_t i = 0; i < kPixels; ++i) {
            if (!std::isfinite(data[i])) throw std::runtime_error("model output contains non-finite values");
            lo = std::min(lo, data[i]); hi = std::max(hi, data[i]);
        }
        raw.resize(kSize, kSize);
        const float range = hi - lo;
        for (size_t i = 0; i < kPixels; ++i) {
            const size_t source = (kSize - 1 - i / kSize) * kSize + i % kSize;
            raw.data()[i] = range > 1e-8f ? std::clamp((data[source] - lo) / range, 0.0f, 1.0f) : 0.0f;
        }
        out.resize(image.width(), image.height());
        resizeBicubic(raw, out);
    }
};

U2NetSegmenter::U2NetSegmenter() = default;
U2NetSegmenter::~U2NetSegmenter() = default;
Result U2NetSegmenter::open(const std::string& runtime, const std::string& model) {
    impl_.reset();
    try {
        if (!fileExists(runtime) || !fileExists(model)) return Result::fail(Status::NotFound, "AI runtime or model is missing");
        auto impl = std::make_unique<Impl>();
        impl->load(runtime, model);
        impl_ = std::move(impl);
        return Result::success();
    } catch (const std::exception& e) { return Result::fail(Status::BackendError, e.what()); }
}
Result U2NetSegmenter::acquire(const FloatImage& image, Matte& out) {
    if (!impl_) return Result::fail(Status::InvalidState, "AI session is not loaded");
    if (image.empty()) return Result::fail(Status::InvalidArgument, "empty AI input image");
    try { impl_->infer(image, out); return Result::success(); }
    catch (const std::exception& e) { return Result::fail(Status::BackendError, e.what()); }
}
} // namespace aibgr

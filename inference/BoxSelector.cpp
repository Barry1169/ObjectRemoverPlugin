// SPDX-License-Identifier: MIT
#include "inference/BoxSelector.h"
#include "matte/Resize.h"
#include "util/Hash.h"
#include "util/Paths.h"
#include <onnxruntime_c_api.h>
#include <algorithm>
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

using GetApiBaseFn = const OrtApiBase*(ORT_API_CALL*)();

/// Host-supplied project data is untrusted, so the number of AI prompts that may reach the
/// decoder is bounded here as well as in the viewer overlay.
constexpr size_t kMaxPrompts = 32;
/// Long edge of the image handed to the encoder. The official EfficientSAM-Ti export resizes any
/// input to its own square working resolution internally, so this only bounds the cost of the
/// resample and of the embedding.
constexpr int kMaxEncoderEdge = 1024;

struct Value {
    const OrtApi* api;
    OrtValue* v = nullptr;
    ~Value() { if (v) api->ReleaseValue(v); }
};
struct Shape {
    const OrtApi* api;
    OrtTensorTypeAndShapeInfo* v = nullptr;
    ~Shape() { if (v) api->ReleaseTensorTypeAndShapeInfo(v); }
};

/// Cache keys only need a good hash, not a cryptographic one; see util/Hash.h.
inline uint64_t hashBytes(uint64_t hash, const void* data, size_t bytes) noexcept {
    return fastHashBytes(hash, data, bytes);
}

} // namespace

struct BoxSelector::Impl {
#if defined(_WIN32)
    HMODULE library = nullptr;
#else
    void* library = nullptr;
#endif
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtSessionOptions* options = nullptr;
    OrtSession* encoder = nullptr;
    OrtSession* decoder = nullptr;
    OrtMemoryInfo* memory = nullptr;
    OrtValue* embedding = nullptr;
    uint64_t embeddingHash = 0;
    bool embeddingValid = false;
    FloatImage scaled;
    Matte small;
    Matte full;
    std::vector<float> input;
    uint64_t maskHash = 0;
    bool maskValid = false;

    ~Impl() {
        if (api) {
            if (embedding) api->ReleaseValue(embedding);
            if (memory) api->ReleaseMemoryInfo(memory);
            if (decoder) api->ReleaseSession(decoder);
            if (encoder) api->ReleaseSession(encoder);
            if (options) api->ReleaseSessionOptions(options);
            if (env) api->ReleaseEnv(env);
        }
#if defined(_WIN32)
        if (library) FreeLibrary(library);
#else
        if (library) dlclose(library);
#endif
    }

    void check(OrtStatus* status) {
        if (!status) return;
        std::string message = api->GetErrorMessage(status);
        api->ReleaseStatus(status);
        throw std::runtime_error(message);
    }

    void load(const std::string& runtime, const std::string& encoderPath,
              const std::string& decoderPath) {
#if defined(_WIN32)
        const auto runtimeWide = widen(runtime);
        library = runtimeWide.empty()
                      ? nullptr
                      : LoadLibraryExW(runtimeWide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        auto getApiBase =
            library ? reinterpret_cast<GetApiBaseFn>(GetProcAddress(library, "OrtGetApiBase"))
                    : nullptr;
#else
        library = dlopen(runtime.c_str(), RTLD_NOW | RTLD_LOCAL);
        auto getApiBase =
            library ? reinterpret_cast<GetApiBaseFn>(dlsym(library, "OrtGetApiBase")) : nullptr;
#endif
        if (!getApiBase) throw std::runtime_error("ONNX Runtime unavailable");
        const OrtApiBase* base = getApiBase();
        api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
        if (!api) throw std::runtime_error("ONNX Runtime 1.16 or newer required");

        check(api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "aibgr-box", &env));
        check(api->CreateSessionOptions(&options));
        check(api->SetIntraOpNumThreads(options, 2));
#if defined(_WIN32)
        const auto encoderWide = widen(encoderPath);
        const auto decoderWide = widen(decoderPath);
        if (encoderWide.empty() || decoderWide.empty()) {
            throw std::runtime_error("invalid model path");
        }
        check(api->CreateSession(env, encoderWide.c_str(), options, &encoder));
        check(api->CreateSession(env, decoderWide.c_str(), options, &decoder));
#else
        check(api->CreateSession(env, encoderPath.c_str(), options, &encoder));
        check(api->CreateSession(env, decoderPath.c_str(), options, &decoder));
#endif

        size_t inputs = 0, outputs = 0;
        check(api->SessionGetInputCount(encoder, &inputs));
        check(api->SessionGetOutputCount(encoder, &outputs));
        if (inputs != 1 || outputs != 1) {
            throw std::runtime_error("unexpected box encoder interface");
        }
        check(api->SessionGetInputCount(decoder, &inputs));
        check(api->SessionGetOutputCount(decoder, &outputs));
        if (inputs != 4 || outputs < 2) {
            throw std::runtime_error("unexpected box decoder interface");
        }
        check(api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory));
    }

    void expectShape(OrtValue* value, const std::vector<int64_t>& expected,
                     ONNXTensorElementDataType dtype) {
        if (!value) throw std::runtime_error("missing box model output");
        Shape shape{api};
        check(api->GetTensorTypeAndShape(value, &shape.v));
        size_t rank = 0;
        ONNXTensorElementDataType type;
        check(api->GetDimensionsCount(shape.v, &rank));
        check(api->GetTensorElementType(shape.v, &type));
        if (rank != expected.size() || type != dtype) {
            throw std::runtime_error("unexpected box model output type or rank");
        }
        std::vector<int64_t> dims(rank);
        check(api->GetDimensions(shape.v, dims.data(), rank));
        if (dims != expected) throw std::runtime_error("unexpected box model output shape");
    }

    /// Downscale to at most kMaxEncoderEdge and run the image encoder, reusing the embedding while
    /// its input is unchanged. `scaled` (bottom-up, like every FloatImage here) is also the guide
    /// for the final edge-aware upscale, so it is kept even when the embedding is cached.
    void encode(const FloatImage& image) {
        const double scale =
            std::min(1.0, double(kMaxEncoderEdge) / std::max(image.width(), image.height()));
        const int w = std::max(1, int(std::round(image.width() * scale)));
        const int h = std::max(1, int(std::round(image.height() * scale)));
        scaled.resize(w, h);
        resizeArea(image, scaled);

        uint64_t hash = hashBytes(14695981039346656037ULL, &w, sizeof(w));
        hash = hashBytes(hash, &h, sizeof(h));
        hash = hashBytes(hash, scaled.data(), scaled.floatCount() * sizeof(float));
        if (embeddingValid && hash == embeddingHash) return;

        embeddingValid = false;
        maskValid = false;
        if (embedding) {
            api->ReleaseValue(embedding);
            embedding = nullptr;
        }

        // NCHW, RGB, top-down. OFX frames and FloatImage rows run bottom-up, and the model was
        // trained on upright images, so both the input and every prompt coordinate are flipped.
        input.resize(size_t(w) * size_t(h) * 3);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float* pixel = scaled.pixel(x, h - 1 - y);
                for (int c = 0; c < 3; ++c) {
                    const float v = pixel[c];
                    input[(size_t(c) * size_t(h) + size_t(y)) * size_t(w) + size_t(x)] =
                        std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
                }
            }
        }

        const int64_t dims[] = {1, 3, h, w};
        Value tensor{api};
        check(api->CreateTensorWithDataAsOrtValue(memory, input.data(),
                                                  input.size() * sizeof(float), dims, 4,
                                                  ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &tensor.v));
        const char* inputName = "batched_images";
        const char* outputName = "image_embeddings";
        const OrtValue* value = tensor.v;
        check(api->Run(encoder, nullptr, &inputName, &value, 1, &outputName, 1, &embedding));

        // The official export always produces a 64x64 embedding regardless of input aspect: the
        // graph resizes internally to its 1024x1024 working square.
        expectShape(embedding, {1, 256, 64, 64}, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
        embeddingHash = hash;
        embeddingValid = true;
    }

    /// Run one prompt (a box, or a single positive point) and paint its best mask into `small`.
    ///
    /// A box is expressed the way the SAM decoder expects: two points labelled 2 and 3. A point
    /// prompt is one point labelled 1. Points are always sent as positive prompts and the keep /
    /// remove decision is applied here by union or subtraction, because a decoder run whose only
    /// prompt is a *negative* point returns the complement of the subject (everything else), which
    /// would be unusable as a subtraction.
    void runPrompt(float* coords, float* labels, int count, bool box, bool keep,
                   bool recoverDetail, int w, int h) {
        int64_t original[] = {h, w};
        const int64_t coordinateDims[] = {1, 1, count, 2};
        const int64_t labelDims[] = {1, 1, count};
        const int64_t sizeDims[] = {2};

        Value points{api}, pointLabels{api}, imageSize{api}, mask{api}, iou{api};
        check(api->CreateTensorWithDataAsOrtValue(memory, coords,
                                                  sizeof(float) * size_t(count) * 2, coordinateDims,
                                                  4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                                  &points.v));
        check(api->CreateTensorWithDataAsOrtValue(memory, labels, sizeof(float) * size_t(count),
                                                  labelDims, 3,
                                                  ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                                  &pointLabels.v));
        check(api->CreateTensorWithDataAsOrtValue(memory, original, sizeof(original), sizeDims, 1,
                                                  ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,
                                                  &imageSize.v));

        const char* names[] = {"image_embeddings", "batched_point_coords", "batched_point_labels",
                               "orig_im_size"};
        const OrtValue* values[] = {embedding, points.v, pointLabels.v, imageSize.v};
        const char* outputs[] = {"output_masks", "iou_predictions"};
        OrtValue* results[] = {nullptr, nullptr};
        check(api->Run(decoder, nullptr, names, values, 4, outputs, 2, results));
        mask.v = results[0];
        iou.v = results[1];

        expectShape(mask.v, {1, 1, 3, h, w}, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
        expectShape(iou.v, {1, 1, 3}, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);

        float* predictions = nullptr;
        float* scores = nullptr;
        check(api->GetTensorMutableData(mask.v, reinterpret_cast<void**>(&predictions)));
        check(api->GetTensorMutableData(iou.v, reinterpret_cast<void**>(&scores)));
        if (!predictions || !scores || !std::isfinite(scores[0]) || !std::isfinite(scores[1]) ||
            !std::isfinite(scores[2])) {
            throw std::runtime_error("invalid box output");
        }

        int best = 0;
        for (int i = 1; i < 3; ++i) {
            if (scores[i] > scores[best]) best = i;
        }
        const size_t plane = size_t(w) * size_t(h);
        const float* result = predictions + plane * size_t(best);

        // SAM exposes three hypotheses. Detail recovery (explicitly opt-in, boxes only) unions in
        // the most similar reasonably scored alternate; a wildly different object is never merged.
        int alternate = -1;
        if (recoverDetail && box && keep) {
            double bestOverlap = 0.45;
            for (int candidate = 0; candidate < 3; ++candidate) {
                if (candidate == best || scores[candidate] < scores[best] - 0.4f) continue;
                size_t intersection = 0, uni = 0;
                const float* other = predictions + plane * size_t(candidate);
                for (size_t i = 0; i < plane; ++i) {
                    if (!std::isfinite(result[i]) || !std::isfinite(other[i])) {
                        throw std::runtime_error("non-finite selection output");
                    }
                    const bool a = result[i] > 0, b = other[i] > 0;
                    intersection += a && b;
                    uni += a || b;
                }
                const double overlap = uni ? double(intersection) / double(uni) : 0.0;
                if (overlap > bestOverlap) {
                    bestOverlap = overlap;
                    alternate = candidate;
                }
            }
        }

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t i = size_t(h - 1 - y) * size_t(w) + size_t(x);
                const float v = result[i];
                if (!std::isfinite(v)) throw std::runtime_error("non-finite selection output");
                const bool extra =
                    alternate >= 0 && predictions[plane * size_t(alternate) + i] > 0;
                if (v > 0 || extra) small.at(x, y) = keep ? 1.0f : 0.0f;
            }
        }
    }

    void calculate(const FloatImage& image, const std::vector<BrushStroke>& strokes,
                   const FrameContext& ctx, bool recoverDetail, Matte& matte) {
        const int iw = image.width();
        const int ih = image.height();

        // Key the cached matte on everything it depends on: the frame content itself (it guides the
        // edge-aware upscale), the coordinate mapping, the prompt list and the detail option. This
        // is computed BEFORE encode() so a cache hit never pays for the downscale that only the
        // encoder and the upscale guide need.
        uint64_t hash = 14695981039346656037ULL;
        hash = hashBytes(hash, &ctx.renderScaleX, sizeof(ctx.renderScaleX));
        hash = hashBytes(hash, &ctx.renderScaleY, sizeof(ctx.renderScaleY));
        hash = hashBytes(hash, &ctx.pixelAspectX, sizeof(ctx.pixelAspectX));
        hash = hashBytes(hash, &ctx.renderWindow.x1, sizeof(ctx.renderWindow.x1));
        hash = hashBytes(hash, &ctx.renderWindow.y1, sizeof(ctx.renderWindow.y1));
        hash = hashBytes(hash, &iw, sizeof(iw));
        hash = hashBytes(hash, &ih, sizeof(ih));
        hash = hashBytes(hash, image.data(), image.floatCount() * sizeof(float));
        hash = hashBytes(hash, &recoverDetail, sizeof(recoverDetail));
        const std::string key = serialiseBrushStrokes(strokes);
        hash = hashBytes(hash, key.data(), key.size());
        if (maskValid && hash == maskHash && full.width() == iw && full.height() == ih) {
            std::copy(full.data(), full.data() + full.floatCount(), matte.data());
            return;
        }

        encode(image);
        const int w = scaled.width();
        const int h = scaled.height();

        // Canonical (render-scale) coordinates to image pixels. sx divides by the pixel aspect
        // because canonical space is defined as square pixels, while the frame may not be.
        const double sx = ctx.renderScaleX / std::max(ctx.pixelAspectX, 1e-8);
        const double sy = ctx.renderScaleY;
        if (!(sx > 0 && sy > 0 && std::isfinite(sx) && std::isfinite(sy))) {
            throw std::runtime_error("invalid selection coordinates");
        }

        small.resize(w, h);
        small.fill(0.0f);
        size_t prompts = 0;
        bool hasAddPrompt = false;

        for (const auto& stroke : strokes) {
            const bool box = stroke.box && stroke.points.size() == 2;
            const bool point = stroke.point && stroke.points.size() == 1;
            if (!box && !point) continue;
            if (++prompts > kMaxPrompts) throw std::runtime_error("maximum 32 AI prompts exceeded");

            auto toX = [&](double v) {
                return float(std::clamp((v * sx - ctx.renderWindow.x1) * w / iw, 0.0,
                                        double(w - 1)));
            };
            auto toY = [&](double v) {
                return float(std::clamp(h - (v * sy - ctx.renderWindow.y1) * h / ih, 0.0,
                                        double(h - 1)));
            };

            if (box) {
                const float x1 = toX(stroke.points[0].x), x2 = toX(stroke.points[1].x);
                const float y1 = toY(stroke.points[0].y), y2 = toY(stroke.points[1].y);
                if (std::abs(x1 - x2) < 1.0f || std::abs(y1 - y2) < 1.0f) continue;
                float coords[] = {std::min(x1, x2), std::min(y1, y2), std::max(x1, x2),
                                  std::max(y1, y2)};
                float labels[] = {2, 3};
                if (stroke.keep) hasAddPrompt = true;
                runPrompt(coords, labels, 2, true, stroke.keep, recoverDetail, w, h);
            } else {
                float coords[] = {toX(stroke.points[0].x), toY(stroke.points[0].y)};
                float labels[] = {1};
                if (stroke.keep) hasAddPrompt = true;
                runPrompt(coords, labels, 1, false, stroke.keep, recoverDetail, w, h);
            }
        }

        if (!hasAddPrompt) throw std::runtime_error("draw an add box or point first");

        full.resize(iw, ih);
        resizeEdgeAware(small, scaled, image, full);
        maskHash = hash;
        maskValid = true;
        std::copy(full.data(), full.data() + full.floatCount(), matte.data());
    }
};

BoxSelector::BoxSelector() = default;
BoxSelector::~BoxSelector() = default;

Result BoxSelector::open(const std::string& runtime, const std::string& encoder,
                         const std::string& decoder) {
    impl_.reset();
    try {
        if (!fileExists(runtime) || !fileExists(encoder) || !fileExists(decoder)) {
            return Result::fail(Status::NotFound, "AI box model/runtime not installed");
        }
        auto impl = std::make_unique<Impl>();
        impl->load(runtime, encoder, decoder);
        impl_ = std::move(impl);
        return Result::success();
    } catch (const std::exception& e) {
        return Result::fail(Status::BackendError, e.what());
    }
}

void BoxSelector::configure(std::vector<BrushStroke> strokes, FrameContext ctx,
                            bool recoverDetail) {
    strokes_ = std::move(strokes);
    ctx_ = ctx;
    recoverDetail_ = recoverDetail;
}

Result BoxSelector::refine(const FloatImage& image, Matte& matte) {
    if (!impl_) return Result::fail(Status::InvalidState, "box model is not loaded");
    if (image.empty() || image.width() != matte.width() || image.height() != matte.height()) {
        return Result::fail(Status::InvalidArgument, "image/matte mismatch");
    }
    try {
        impl_->calculate(image, strokes_, ctx_, recoverDetail_, matte);
        return Result::success();
    } catch (const std::exception& e) {
        return Result::fail(Status::BackendError, e.what());
    }
}

void BoxSelector::clearCache() noexcept {
    if (!impl_) return;
    impl_->maskValid = false;
    impl_->embeddingValid = false;
    if (impl_->embedding) {
        impl_->api->ReleaseValue(impl_->embedding);
        impl_->embedding = nullptr;
    }
    impl_->scaled.release();
    impl_->small.release();
    impl_->full.release();
    std::vector<float>().swap(impl_->input);
}
} // namespace aibgr

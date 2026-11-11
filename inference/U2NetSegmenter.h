// SPDX-License-Identifier: MIT
#pragma once

#include "core/Pipeline.h"
#include <memory>
#include <string>

namespace aibgr {

/// Optional U2-NetP CPU backend. ONNX Runtime is loaded from an explicit user-supplied path.
/// One object/session belongs to one plugin instance and is protected by that instance's lock.
class U2NetSegmenter final : public MatteSource {
public:
    U2NetSegmenter();
    ~U2NetSegmenter() override;
    U2NetSegmenter(const U2NetSegmenter&) = delete;
    U2NetSegmenter& operator=(const U2NetSegmenter&) = delete;

    /// `intraOpThreads` 0 = automatic (all cores, capped at 8). Baked into the ONNX session, so a
    /// new value needs a new open().
    Result open(const std::string& runtimePath, const std::string& modelPath,
                int intraOpThreads = 0);
    Result acquire(const FloatImage& image, Matte& out) override;
    const char* name() const noexcept override { return "U2NetP CPU"; }
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace aibgr

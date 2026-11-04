// SPDX-License-Identifier: MIT
#pragma once
#include "core/Pipeline.h"
#include <filesystem>
#include <string>
#include <vector>

namespace aibgr {
/// Transparent PNG: green paint keeps the subject, red paint removes it, alpha is strength.
/// A per-instance cache; caller must hold the plugin instance's scratch mutex.
class CorrectionMask final : public MatteOverride {
public:
    Result update(const std::string& path, bool& changed);
    Result apply(Matte& matte, bool inverted = false) const override;
    void clear() noexcept;
    bool ready() const noexcept { return !pixels_.empty(); }
private:
    std::string path_;
    std::filesystem::file_time_type modified_{};
    std::uintmax_t bytes_ = 0;
    bool observed_ = false;
    Result result_;
    int width_ = 0, height_ = 0;
    std::vector<unsigned char> pixels_;
};
} // namespace aibgr

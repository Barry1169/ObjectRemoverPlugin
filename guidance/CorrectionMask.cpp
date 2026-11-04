// SPDX-License-Identifier: MIT
#include "guidance/CorrectionMask.h"
#include <algorithm>
#include <fstream>
#include <limits>
#include <memory>
#include <vector>

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 16384
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace aibgr {
namespace {
constexpr std::uintmax_t kMaxFileBytes = 128u * 1024u * 1024u;
constexpr std::uint64_t kMaxPixels = 40u * 1000u * 1000u;
}
void CorrectionMask::clear() noexcept {
    path_.clear(); observed_ = false; bytes_ = 0; width_ = height_ = 0;
    std::vector<unsigned char>().swap(pixels_);
    result_ = Result::success();
}
Result CorrectionMask::update(const std::string& path, bool& changed) {
    changed = false;
    try {
        if (path.empty()) {
            if (observed_ || !path_.empty()) { clear(); changed = true; }
            return Result::success();
        }
        const auto file = std::filesystem::u8path(path);
        std::error_code ec;
        auto modified = std::filesystem::last_write_time(file, ec);
        const bool exists = !ec;
        std::uintmax_t size = exists ? std::filesystem::file_size(file, ec) : 0;
        const bool validStat = exists && !ec;
        if (observed_ && path == path_ && modified == modified_ && size == bytes_) return result_;
        changed = true;
        path_ = path; modified_ = modified; bytes_ = size; observed_ = true;
        pixels_.clear(); width_ = height_ = 0;
        if (!validStat) return result_ = Result::fail(Status::NotFound, "guidance PNG cannot be read");
        if (size == 0 || size > kMaxFileBytes) return result_ = Result::fail(Status::InvalidArgument, "guidance PNG is empty or exceeds 128 MB");
        std::ifstream in(file, std::ios::binary);
        if (!in) return result_ = Result::fail(Status::IoError, "could not open guidance PNG");
        std::vector<unsigned char> data(static_cast<size_t>(size));
        if (!in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size)))
            return result_ = Result::fail(Status::IoError, "could not read guidance PNG");
        int w = 0, h = 0, channels = 0;
        if (!stbi_info_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &channels))
            return result_ = Result::fail(Status::Unsupported, "guidance is not a valid PNG");
        if (w <= 0 || h <= 0 || static_cast<std::uint64_t>(w) * h > kMaxPixels)
            return result_ = Result::fail(Status::InvalidArgument, "guidance image dimensions exceed 40 million pixels");
        if (channels != 4 && channels != 2)
            return result_ = Result::fail(Status::InvalidArgument, "guidance PNG must have an alpha channel");
        using StbiPtr = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
        StbiPtr decoded(stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &channels, 4), &stbi_image_free);
        if (!decoded) return result_ = Result::fail(Status::Unsupported, "could not decode guidance PNG");
        pixels_.assign(decoded.get(), decoded.get() + static_cast<size_t>(w) * h * 4);
        width_ = w; height_ = h;
        return result_ = Result::success();
    } catch (const std::exception& e) {
        pixels_.clear(); width_ = height_ = 0; changed = true;
        return result_ = Result::fail(Status::IoError, e.what());
    }
}
Result CorrectionMask::apply(Matte& matte, bool inverted) const {
    if (!ready()) return Result::success();
    if (matte.width() != width_ || matte.height() != height_)
        return Result::fail(Status::InvalidArgument, "guidance PNG size does not match render frame; ignoring guidance");
    // PNG rows are top-down while OFX frames/mattes are bottom-up.
    for (int y = 0; y < height_; ++y) {
        const auto* p = pixels_.data() + static_cast<size_t>(height_ - 1 - y) * width_ * 4;
        float* out = matte.row(y);
        for (int x = 0; x < width_; ++x, p += 4) {
            // Strongly red or green: neutral colours are ignored, including transparent RGB.
            const bool keep = p[1] > p[0] && p[1] > p[2];
            const bool remove = p[0] > p[1] && p[0] > p[2];
            if ((!keep && !remove) || p[3] == 0) continue;
            const float strength = static_cast<float>(p[3]) / 255.0f;
            const bool target = keep != inverted;
            out[x] = out[x] * (1.0f - strength) + (target ? strength : 0.0f);
        }
    }
    return Result::success();
}
} // namespace aibgr

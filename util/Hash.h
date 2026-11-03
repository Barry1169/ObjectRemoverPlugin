// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace aibgr {

/// FNV-1a 64. Chosen because it is tiny, dependency-free, and good enough for cache keys
/// (this is not a security or checksum primitive - model files use SHA-256 separately).
class HashBuilder {
public:
    HashBuilder() = default;
    explicit HashBuilder(uint64_t seed) noexcept : hash_(seed) {}

    HashBuilder& bytes(const void* data, std::size_t n) noexcept {
        const auto* p = static_cast<const uint8_t*>(data);
        for (std::size_t i = 0; i < n; ++i) {
            hash_ ^= static_cast<uint64_t>(p[i]);
            hash_ *= 0x100000001b3ULL;
        }
        return *this;
    }

    HashBuilder& str(std::string_view s) noexcept { return bytes(s.data(), s.size()); }

    HashBuilder& u64(uint64_t v) noexcept { return bytes(&v, sizeof(v)); }
    HashBuilder& i64(int64_t v) noexcept { return bytes(&v, sizeof(v)); }
    HashBuilder& i32(int32_t v) noexcept { return bytes(&v, sizeof(v)); }
    HashBuilder& dbl(double v) noexcept { return bytes(&v, sizeof(v)); }
    HashBuilder& boolean(bool v) noexcept { return bytes(&v, sizeof(v)); }

    /// Mix in a float without -0.0f and +0.0f hashing differently.
    HashBuilder& flt(float v) noexcept {
        if (v == 0.0f) {
            v = 0.0f;
        }
        return bytes(&v, sizeof(v));
    }

    uint64_t value() const noexcept { return hash_; }

private:
    uint64_t hash_ = 0xcbf29ce484222325ULL;
};

inline uint64_t hashBytes(const void* data, std::size_t n) noexcept {
    return HashBuilder().bytes(data, n).value();
}

} // namespace aibgr

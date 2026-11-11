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

/// Avalanche mixer for 64-bit values (splitmix64 finaliser).
inline uint64_t mixHashBits(uint64_t x) noexcept {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

/// Fast streaming content hash for large buffers, chainable through `seed`.
///
/// FNV-1a walks one byte per iteration through a single dependency chain, which is latency-bound at
/// roughly 1 byte/cycle. That is fine for a handful of parameters but not for a whole 4K frame,
/// where hashing dominated every cached render (measured: ~160 ms of a ~225 ms cache hit). Four
/// independent 64-bit lanes over 32-byte blocks hide the multiply latency, bringing the same buffer
/// down to a few milliseconds.
///
/// Chaining is well defined: `fastHashBytes(fastHashBytes(seed, a, na), b, nb)` is a fixed function
/// of the sequence, which is what the callers need. Like the rest of this file it is a cache key,
/// not a digest: the requirement is that any plausible change alters the result, and collisions are
/// only expensive (a stale cached matte), never unsafe.
inline uint64_t fastHashBytes(uint64_t seed, const void* data, std::size_t n) noexcept {
    constexpr uint64_t kPrime = 1099511628211ULL;
    const auto* p = static_cast<const uint8_t*>(data);
    const std::size_t total = n;
    uint64_t a = seed ^ 0x243f6a8885a308d3ULL;
    uint64_t b = seed + 0x13198a2e03707344ULL;
    uint64_t c = seed ^ 0xa4093822299f31d0ULL;
    uint64_t d = seed + 0x082efa98ec4e6c89ULL;
    while (n >= 32) {
        uint64_t w[4];
        std::memcpy(w, p, sizeof(w));
        a = (a ^ w[0]) * kPrime;
        b = (b ^ w[1]) * kPrime;
        c = (c ^ w[2]) * kPrime;
        d = (d ^ w[3]) * kPrime;
        p += 32;
        n -= 32;
    }
    if (n) {
        // The remainder is shorter than a block: stage it in a zero-filled block so the lanes can
        // consume whole words without reading past the end of the caller's buffer.
        uint8_t tail[32] = {};
        std::memcpy(tail, p, n);
        uint64_t w[4];
        std::memcpy(w, tail, sizeof(w));
        a = (a ^ w[0]) * kPrime;
        b = (b ^ w[1]) * kPrime;
        c = (c ^ w[2]) * kPrime;
        d = (d ^ w[3]) * kPrime;
    }
    // The length is folded in so a shorter buffer cannot alias a longer one that ends in zeros.
    return mixHashBits(a ^ mixHashBits(b) ^ mixHashBits(c) ^ mixHashBits(d) ^ total);
}

} // namespace aibgr

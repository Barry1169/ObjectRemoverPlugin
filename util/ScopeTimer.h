// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>

namespace aibgr {

/// Minimal wall-clock scope timer, used to fill PipelineStats. Not a profiler: it exists so the
/// Diagnostics line can report where a frame's time actually went.
class ScopeTimer {
public:
    ScopeTimer() noexcept : start_(clock::now()) {}

    void reset() noexcept { start_ = clock::now(); }

    double elapsedMs() const noexcept {
        const auto now = clock::now();
        return std::chrono::duration<double, std::milli>(now - start_).count();
    }

    /// Milliseconds since the last lap(), and start a new lap.
    double lapMs() noexcept {
        const double ms = elapsedMs();
        reset();
        return ms;
    }

private:
    using clock = std::chrono::steady_clock;
    clock::time_point start_;
};

} // namespace aibgr

// SPDX-License-Identifier: MIT
#pragma once

// A ~120-line test harness. No dependency to fetch, no CMake find_package, nothing that can be
// missing on a fresh Windows checkout: the requirement is that `cmake --build build && ctest` works
// the first time, on a machine with no internet access to a package registry.
//
// Usage:
//   TEST(premultiply, zeroAlphaProducesBlack) {
//       CHECK_NEAR(out[0], 0.0f, 1e-6f);
//   }
//
// Every CHECK records a failure and CONTINUES, so one broken build shows every problem at once
// rather than one per run.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace aibgr {
namespace test {

using TestFn = void (*)();

class Registry {
public:
    static Registry& instance() {
        static Registry r;
        return r;
    }

    void add(const char* suite, const char* name, TestFn fn) {
        entries_.push_back(Entry{suite, name, fn});
    }

    /// Records one failed check. Public so the CHECK macros can reach it without friendship games.
    void recordFailure(const std::string& where, const std::string& msg) {
        failures_.push_back(where + ": " + msg);
    }

    /// Runs everything. Returns the number of failed checks (0 = all good), which main() turns
    /// into a process exit code for ctest.
    int runAll(const char* filter) {
        int failedChecks = 0;
        int ran = 0;
        for (const Entry& e : entries_) {
            const std::string full = e.suite + "." + e.name;
            if (filter != nullptr && filter[0] != '\0' && full.find(filter) == std::string::npos) {
                continue;
            }
            ++ran;
            failuresBefore_ = failures_.size();
            e.fn();
            const size_t added = failures_.size() - failuresBefore_;
            if (added == 0) {
                std::printf("  ok   %s\n", full.c_str());
            } else {
                std::printf("  FAIL %s\n", full.c_str());
                for (size_t i = failuresBefore_; i < failures_.size(); ++i) {
                    std::printf("         %s\n", failures_[i].c_str());
                }
            }
            failedChecks += static_cast<int>(added);
        }
        std::printf("\n%d test(s) run, %d check(s) failed\n", ran, failedChecks);
        return failedChecks;
    }

private:
    struct Entry {
        std::string suite;
        std::string name;
        TestFn fn;
    };
    std::vector<Entry> entries_;
    std::vector<std::string> failures_;
    size_t failuresBefore_ = 0;
};

inline void reportFailure(const char* file, int line, const std::string& msg) {
    Registry::instance().recordFailure(std::string(file) + ":" + std::to_string(line), msg);
}

inline std::string describe(const char* expr, double a, double b) {
    return std::string(expr) + " -> got " + std::to_string(a) + ", expected " + std::to_string(b);
}

} // namespace test
} // namespace aibgr

#define AIBGR_TEST_REG(suite, name)                                                            \
    static void suite##_##name##_body();                                                       \
    static const bool suite##_##name##_registered = [] {                                       \
        ::aibgr::test::Registry::instance().add(#suite, #name, &suite##_##name##_body);         \
        return true;                                                                           \
    }();                                                                                       \
    static void suite##_##name##_body()

#define TEST(suite, name) AIBGR_TEST_REG(suite, name)

#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            ::aibgr::test::reportFailure(__FILE__, __LINE__, "CHECK failed: " #cond);          \
        }                                                                                      \
    } while (false)

#define CHECK_MSG(cond, msg)                                                                   \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            ::aibgr::test::reportFailure(__FILE__, __LINE__,                                   \
                                         std::string("CHECK failed: ") + (msg));               \
        }                                                                                      \
    } while (false)

#define CHECK_EQ(a, b)                                                                         \
    do {                                                                                       \
        const auto va_ = (a);                                                                  \
        const auto vb_ = (b);                                                                  \
        if (!(va_ == vb_)) {                                                                   \
            ::aibgr::test::reportFailure(__FILE__, __LINE__,                                   \
                                         #a " == " #b " failed");                              \
        }                                                                                      \
    } while (false)

#define CHECK_NEAR(a, b, eps)                                                                  \
    do {                                                                                       \
        const double va_ = static_cast<double>(a);                                             \
        const double vb_ = static_cast<double>(b);                                             \
        if (!(std::fabs(va_ - vb_) <= static_cast<double>(eps))) {                             \
            ::aibgr::test::reportFailure(__FILE__, __LINE__,                                   \
                                         ::aibgr::test::describe(#a " ~= " #b, va_, vb_));     \
        }                                                                                      \
    } while (false)

/// Fails if a and b differ by even one ULP. Used for the "no colour shift" guarantees, where an
/// approximate comparison would hide exactly the bug being tested for.
#define CHECK_EXACT(a, b)                                                                      \
    do {                                                                                       \
        const float va_ = (a);                                                                 \
        const float vb_ = (b);                                                                 \
        if (!(va_ == vb_)) {                                                                   \
            ::aibgr::test::reportFailure(__FILE__, __LINE__,                                   \
                                         #a " is bit-identical to " #b);                       \
        }                                                                                      \
    } while (false)

// SPDX-License-Identifier: MIT
#pragma once

#include <cstdarg>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace aibgr {

enum class LogLevel : int { Off = 0, Error = 1, Info = 2, Verbose = 3 };

/// The "debug log trick" from the field guide: you cannot easily attach a debugger to a plugin
/// running inside Resolve, so every action and every interesting decision is appended to a text
/// file. The file is opened, written and closed for each line so that nothing is lost if the host
/// dies mid-render.
///
/// Thread safety: all public methods take an internal mutex. The logger never throws and never
/// calls back into the host.
class Log {
public:
    static Log& instance() noexcept;

    void setLevel(LogLevel level) noexcept;
    LogLevel level() const noexcept;

    /// Empty path disables file output entirely (the default in a release install).
    void setFilePath(std::string utf8Path);
    std::string filePath() const;

    /// Action trace: the single most useful diagnostic when a plugin "does nothing" in Resolve.
    void setTraceActions(bool enabled) noexcept;
    bool traceActions() const noexcept;

    void traceAction(std::string_view action) noexcept;

    void write(LogLevel level, std::string_view message) noexcept;
    void writef(LogLevel level, const char* fmt, ...) noexcept
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 3, 4)))
#endif
        ;
    void writev(LogLevel level, const char* fmt, va_list args) noexcept;

    /// Prefix stamped on every line, e.g. the plugin instance pointer, so interleaved instances
    /// can be told apart.
    void setContext(std::string context);

    bool enabled(LogLevel level) const noexcept {
        return level != LogLevel::Off && static_cast<int>(level) <= static_cast<int>(level_);
    }

private:
    Log() noexcept;
    ~Log();
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    void appendLine(std::string_view line) noexcept;

    mutable std::mutex mutex_;
    std::string path_;
    std::string context_;
    LogLevel level_ = LogLevel::Error;
    bool traceActions_ = true;
    uint64_t lineCount_ = 0;
};

/// printf-style formatting into a std::string, never throwing.
std::string formatString(const char* fmt, ...) noexcept
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
std::string formatStringV(const char* fmt, va_list args) noexcept;

} // namespace aibgr

#define AIBGR_LOGE(...) ::aibgr::Log::instance().writef(::aibgr::LogLevel::Error, __VA_ARGS__)
#define AIBGR_LOGI(...) ::aibgr::Log::instance().writef(::aibgr::LogLevel::Info, __VA_ARGS__)
#define AIBGR_LOGV(...) ::aibgr::Log::instance().writef(::aibgr::LogLevel::Verbose, __VA_ARGS__)

/// Verbose, spelled "debug" at call sites where the intent is developer diagnostics rather than
/// something an artist would ever want in a shipped log.
#define AIBGR_LOGD(...) ::aibgr::Log::instance().writef(::aibgr::LogLevel::Verbose, __VA_ARGS__)

// SPDX-License-Identifier: MIT
#include "util/Log.h"

#include "util/Paths.h"

#include <chrono>
#include <cstdio>
#include <cstring>

namespace aibgr {

namespace {

constexpr std::size_t kStackFormatBuffer = 2048;

const char* levelTag(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Off: return "     ";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Info: return "INFO ";
    case LogLevel::Verbose: return "VERB ";
    }
    return "?????";
}

/// Local time-of-day stamp. Deliberately avoids std::put_time / locales: this runs inside a host
/// process whose locale we do not control.
std::string timeStamp() noexcept {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    const std::time_t tt = system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    ::localtime_s(&tm, &tt);
#else
    ::localtime_r(&tt, &tm);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03lld", tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<long long>(ms));
    return std::string(buf);
}

} // namespace

std::string formatStringV(const char* fmt, va_list args) noexcept {
    char stack[kStackFormatBuffer];
    va_list copy;
    va_copy(copy, args);
    const int needed = std::vsnprintf(stack, sizeof(stack), fmt, copy);
    va_end(copy);
    if (needed < 0) {
        return {};
    }
    if (static_cast<std::size_t>(needed) < sizeof(stack)) {
        return std::string(stack, static_cast<std::size_t>(needed));
    }
    // Message did not fit the stack buffer; heap-allocate exactly once.
    std::string out(static_cast<std::size_t>(needed) + 1, '\0');
    va_list copy2;
    va_copy(copy2, args);
    std::vsnprintf(out.data(), out.size(), fmt, copy2);
    va_end(copy2);
    out.resize(static_cast<std::size_t>(needed));
    return out;
}

std::string formatString(const char* fmt, ...) noexcept {
    va_list args;
    va_start(args, fmt);
    std::string out = formatStringV(fmt, args);
    va_end(args);
    return out;
}

Log& Log::instance() noexcept {
    // A function-local static is initialised thread-safely by the compiler (C++11 magic statics),
    // which matters because Resolve may create plugin instances on several threads.
    static Log log;
    return log;
}

Log::Log() noexcept {
#if defined(AIBGR_TRACE_ACTIONS_DEFAULT)
    traceActions_ = (AIBGR_TRACE_ACTIONS_DEFAULT != 0);
#endif
}

Log::~Log() = default;

void Log::setLevel(LogLevel level) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Log::level() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

void Log::setFilePath(std::string utf8Path) {
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = std::move(utf8Path);
    lineCount_ = 0;
}

std::string Log::filePath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return path_;
}

void Log::setTraceActions(bool enabled) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    traceActions_ = enabled;
}

bool Log::traceActions() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return traceActions_;
}

void Log::setContext(std::string context) {
    std::lock_guard<std::mutex> lock(mutex_);
    context_ = std::move(context);
}

void Log::traceAction(std::string_view action) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!traceActions_ || path_.empty()) {
            return;
        }
    }
    std::string line = "ACTION  ";
    line.append(action);
    appendLine(line);
}

void Log::write(LogLevel level, std::string_view message) noexcept {
    if (!enabled(level)) {
        return;
    }
    std::string line(levelTag(level));
    line += ' ';
    line.append(message);
    appendLine(line);
}

void Log::writef(LogLevel level, const char* fmt, ...) noexcept {
    if (!enabled(level)) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    writev(level, fmt, args);
    va_end(args);
}

void Log::writev(LogLevel level, const char* fmt, va_list args) noexcept {
    if (!enabled(level)) {
        return;
    }
    std::string line(levelTag(level));
    line += ' ';
    line += formatStringV(fmt, args);
    appendLine(line);
}

void Log::appendLine(std::string_view line) noexcept {
    // Snapshot the mutable fields under the lock, then do the (potentially slow) file I/O with a
    // separate guard so a stalled disk cannot corrupt state. Simplicity wins here: this is a
    // diagnostic path, not a render path.
    std::string path;
    std::string context;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (path_.empty()) {
            return;
        }
        path = path_;
        context = context_;
        ++lineCount_;
    }

    std::string stamped;
    stamped.reserve(line.size() + context.size() + 32);
    stamped += timeStamp();
    stamped += ' ';
    if (!context.empty()) {
        stamped += context;
        stamped += ' ';
    }
    stamped.append(line);
    stamped += '\n';

    std::lock_guard<std::mutex> lock(mutex_);
#if defined(_WIN32)
    FILE* f = nullptr;
    const std::wstring widePath = widen(path);
    if (widePath.empty() || ::_wfopen_s(&f, widePath.c_str(), L"a") != 0 || f == nullptr) {
        return;
    }
#else
    FILE* f = std::fopen(path.c_str(), "a");
    if (f == nullptr) {
        return;
    }
#endif
    std::fwrite(stamped.data(), 1, stamped.size(), f);
    std::fflush(f);
    std::fclose(f);
}

} // namespace aibgr

// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace aibgr {

/// Lightweight, non-throwing result type. Nothing in the plugin may let a C++ exception
/// cross into the host (docs/03-tradeoffs-and-decisions.md, D9), so internal layers report
/// failure with this instead.
enum class Status : uint8_t {
    Ok = 0,
    NotFound,      // model file, onnxruntime.dll, ...
    InvalidArgument,
    InvalidState,
    IoError,
    OutOfMemory,
    Unsupported,   // e.g. an execution provider this ONNX Runtime build lacks
    BackendError,  // ONNX Runtime returned a non-OK OrtStatus
    Cancelled,     // host asked us to abort
};

inline const char* statusName(Status s) noexcept {
    switch (s) {
    case Status::Ok: return "Ok";
    case Status::NotFound: return "NotFound";
    case Status::InvalidArgument: return "InvalidArgument";
    case Status::InvalidState: return "InvalidState";
    case Status::IoError: return "IoError";
    case Status::OutOfMemory: return "OutOfMemory";
    case Status::Unsupported: return "Unsupported";
    case Status::BackendError: return "BackendError";
    case Status::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

struct Result {
    Status status = Status::Ok;
    std::string message;

    Result() = default;
    Result(Status s, std::string m) : status(s), message(std::move(m)) {}

    explicit operator bool() const noexcept { return status == Status::Ok; }
    bool ok() const noexcept { return status == Status::Ok; }

    static Result success() { return Result{}; }
    static Result fail(Status s, std::string m) { return Result{s, std::move(m)}; }

    /// One-line human readable form, safe to hand to an OFX message or the log.
    std::string toString() const {
        std::string out(statusName(status));
        if (!message.empty()) {
            out += ": ";
            out += message;
        }
        return out;
    }
};

} // namespace aibgr

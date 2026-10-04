// SPDX-License-Identifier: GPL-3.0-or-later
// Runtime binding of a probe spec (ADR-0006). Only available when K2_DIAG_HAS_RUNTIME=1.
#pragma once

#include "k2/diag/probe_log.h"
#include "k2/diag/probe_spec.h"
#include "k2/diag/write_attribution.h"

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class PS2Runtime;

namespace k2::diag {

struct FrameActions {
    bool screenshot = false; // caller saves the presented frame now
};

class ProbeSession {
public:
    // Wraps the spec's functions and attaches the dispatch observer (only if the spec has
    // attrib ranges). Call after the ELF is loaded and before PS2Runtime::run(). Fails closed if
    // a func address has no recompiled function. At most one session per process.
    static std::expected<std::unique_ptr<ProbeSession>, std::string>
    install(PS2Runtime &runtime, const ProbeSpec &spec, ProbeLog &log);

    ProbeSession(const ProbeSession &) = delete;
    ProbeSession &operator=(const ProbeSession &) = delete;
    ~ProbeSession();

    // Render thread, once per presented frame (n = 1, 2, ...): records the frame number used
    // to tag every line, prints due watches and says whether to take a screenshot.
    FrameActions on_presented_frame(PS2Runtime &runtime, std::uint32_t n);

    // After run() returned: writes the sorted attribution summary and an end line, flushes.
    void finish();

    [[nodiscard]] std::uint32_t frame() const { return frame_.load(std::memory_order_relaxed); }

private:
    ProbeSession(PS2Runtime &runtime, const ProbeSpec &spec, ProbeLog &log);

    PS2Runtime &runtime_;
    ProbeSpec spec_;
    ProbeLog &log_;
    std::atomic<std::uint32_t> frame_{0};
    std::optional<WriteAttributor> attrib_;
    std::vector<WriteEvent> scratch_;
    bool finished_ = false;

    friend struct SessionAccess;
};

} // namespace k2::diag

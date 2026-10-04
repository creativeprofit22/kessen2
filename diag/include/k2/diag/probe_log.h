// SPDX-License-Identifier: GPL-3.0-or-later
// Line-oriented probe output: "frame=F seq=N kind=K key=value...". Thread-safe; seq is global.
#pragma once

#include "k2/diag/write_attribution.h"

#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace k2::diag {

class ProbeLog {
public:
    using Sink = std::function<void(std::string_view line)>; // receives one line incl. '\n'

    explicit ProbeLog(Sink sink);

    // Empty path = stderr with prefix "[k2probe] "; otherwise a new/truncated file.
    static std::expected<std::unique_ptr<ProbeLog>, std::string> open(const std::filesystem::path &path);

    // Writes one line. `fields` is "key=value ..." (may be empty).
    void write(std::uint32_t frame, std::string_view kind, std::string_view fields);

    void flush();

private:
    std::mutex mutex_;
    Sink sink_;
    std::uint64_t seq_ = 0;
    std::shared_ptr<std::FILE> file_;
};

// "addr=0x... old=0x.. new=0x.. writer=0x...|unattributed [site=0x...] edge=enter|exit"
std::string format_write_event(const WriteEvent &event);

} // namespace k2::diag

// SPDX-License-Identifier: GPL-3.0-or-later
// Probe spec v1 (ADR-0006): parsed and validated fail-closed. Game-agnostic.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace k2::diag {

// Hard limits. Any spec exceeding one is rejected (never truncated).
struct Limits {
    static constexpr std::uint32_t kRamBase = 0x00000000u;
    static constexpr std::uint32_t kRamEnd = 0x02000000u; // EE RDRAM, exclusive
    static constexpr std::size_t kMaxFileBytes = 64u * 1024u;
    static constexpr std::size_t kMaxFuncs = 16;
    static constexpr std::uint32_t kMaxCalls = 1024;
    static constexpr std::uint32_t kMaxNonzero = 4096;
    static constexpr std::size_t kMaxWatchRanges = 16;
    static constexpr std::uint32_t kMaxWatchRangeBytes = 64;
    static constexpr std::uint32_t kMaxWatchBytes = 256;
    static constexpr std::size_t kMaxAttribRanges = 16;
    static constexpr std::uint32_t kMaxAttribBytes = 4096;
    static constexpr std::uint32_t kMaxAttribEvents = 1'000'000;
    static constexpr std::uint32_t kDefaultCalls = 64;
    static constexpr std::uint32_t kDefaultWatchEvery = 600;
    static constexpr std::uint32_t kDefaultAttribEvents = 100'000;
};

struct FuncProbe {
    std::uint32_t address = 0;
    std::uint32_t calls = Limits::kDefaultCalls; // logged entry/return pairs per window
    std::uint32_t nonzero = 0;                   // extra returns with v0 != 0 after the window
    std::optional<std::uint32_t> word;           // guest word shown on every line
};

struct ArmProbe {
    std::uint32_t address = 0; // must also be a FuncProbe
    std::uint32_t after = 1;   // start logging at this call number of `address`
};

struct Range {
    std::uint32_t address = 0;
    std::uint32_t length = 0;
    [[nodiscard]] std::uint32_t end() const { return address + length; }
};

struct WatchProbe {
    Range range;
    std::uint32_t every = Limits::kDefaultWatchEvery; // presented frames
};

struct ScreenshotProbe {
    std::uint32_t every = 0;
    std::uint32_t from = 0;
};

// Logs the GS privileged display registers and the presenter's chosen source, same schedule
// rules as ScreenshotProbe.
struct GsRegsProbe {
    std::uint32_t every = 0;
    std::uint32_t from = 0;
};

// Dumps the GS draw and image-transfer events recorded since the previous dump (the engine keeps
// the last 512 GS events), same schedule rules as ScreenshotProbe.
struct GsEventsProbe {
    std::uint32_t every = 0;
    std::uint32_t from = 0;
};

struct ProbeSpec {
    std::vector<FuncProbe> funcs;    // in file order
    std::optional<ArmProbe> arm;
    std::vector<WatchProbe> watches; // sorted by address
    std::vector<Range> attrib;       // sorted by address
    std::uint32_t attrib_max_events = Limits::kDefaultAttribEvents;
    std::optional<ScreenshotProbe> screenshot;
    std::optional<GsRegsProbe> gsregs;
    std::optional<GsEventsProbe> gsevents;

    [[nodiscard]] bool empty() const
    {
        return funcs.empty() && watches.empty() && attrib.empty() && !screenshot && !gsregs && !gsevents;
    }
};

struct SpecError {
    std::size_t line = 0; // 1-based; 0 = whole file
    std::string message;
};

// "line N: message" (or just the message for line 0).
std::string to_string(const SpecError &error);

std::expected<ProbeSpec, SpecError> parse_probe_spec(std::string_view text);

// Reads and parses a spec file; fails closed on I/O errors, oversize files and NUL bytes.
std::expected<ProbeSpec, SpecError> load_probe_file(const std::filesystem::path &path);

} // namespace k2::diag

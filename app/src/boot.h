// SPDX-License-Identifier: GPL-3.0-or-later
// Boots the recompiled game on PS2Recomp's runtime (only built when generated code exists).
#pragma once

#include <cstdint>
#include <string>

namespace k2::app {

struct BootOptions {
    std::string elf_path;
    bool headless = false;     // hidden window + strict missing-function policy
    std::uint32_t frames = 0;  // stop after this many presented frames (0 = run until closed)
    std::uint32_t timeout_s = 0; // watchdog; 0 = none
    std::string probe_path;      // K2_PROBE: probe spec file (ADR-0006); empty = none
    std::string probe_log_path;  // K2_PROBE_LOG: probe output file; empty = stderr
};

enum BootExit : int {
    kBootOk = 0,          // reached --frames, or the window was closed
    kBootInitFailed = 1,  // runtime init or ELF load failed
    kBootStoppedEarly = 3, // guest stopped (crash, missing function, exit) before --frames
    kBootTimeout = 4,     // --timeout-s elapsed first
    // Probe setup failed (ADR-0006): K2_PROBE spec malformed or unreadable; a `func` address with
    // no recompiled function; K2_PROBE empty, or set in a build without game code; K2_PROBE_LOG
    // cannot be created; or a removed legacy trace variable (K2_TRACE_*, K2_WATCH, ...) is set.
    kBootProbeInvalid = 5,
};

// Runs the game and returns a BootExit code. The caller must end the process with std::_Exit
// (like upstream's runner): the runtime is intentionally not destroyed after run().
int boot(const BootOptions &options);

} // namespace k2::app

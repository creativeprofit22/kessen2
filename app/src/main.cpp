// SPDX-License-Identifier: GPL-3.0-or-later
// kessen2 — composition root. Wires platform, render, runtime extensions and (when present)
// recompiled game code together. Keep this file thin.
#include "k2/platform.h"
#include "k2/render.h"
#include "k2/runtime_ext.h"

#if K2_HAS_GENERATED
#include "boot.h"
#endif

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#ifndef K2_VERSION
#define K2_VERSION "0.0.0"
#endif

namespace {

// Where tools/disc/extract.sh puts the retail boot ELF, relative to the repository root.
constexpr std::string_view kDefaultElf = "work/disc/SLUS-20275/iso/SLUS_202.75";

// Diagnostics variables removed by ADR-0006. A set one fails closed (exit 5) instead of being
// silently ignored, so an old command line cannot produce a quiet, misleading run.
constexpr std::array<const char *, 8> kRemovedEnvVars = {
    "K2_TRACE_FUNCS", "K2_TRACE_WORD", "K2_TRACE_ARM", "K2_TRACE_NONZERO",
    "K2_TRACE_CD",    "K2_WATCH",      "K2_WATCH_EVERY", "K2_SCREENSHOT_EVERY",
};

// Returns true (after printing why) if any removed diagnostics variable is set.
bool reject_removed_env_vars()
{
    for (const char *name : kRemovedEnvVars) {
        if (std::getenv(name) != nullptr) {
            std::fprintf(stderr,
                         "kessen2: %s was removed; use a probe file (see the mapping in "
                         "docs/BRINGUP.md \"Diagnostics\", probes/README.md)\n",
                         name);
            return true;
        }
    }
    return false;
}

void print_version()
{
    std::printf("kessen2 %s\n", K2_VERSION);
    std::printf("SDL %s (built against %s)\n",
                k2::platform::to_string(k2::platform::sdl_linked_version()).c_str(),
                k2::platform::to_string(k2::platform::sdl_compiled_version()).c_str());
    std::printf("render: %s\n", k2::render::backend_name());
    std::printf("runtime-ext: %d override(s)\n", k2::runtime_ext::override_count());
    std::printf("generated code: %s\n", K2_HAS_GENERATED ? "linked" : "not present");
}

void print_usage()
{
    std::printf("usage: kessen2 [--version | --help]\n");
#if K2_HAS_GENERATED
    std::printf("       kessen2 [--elf <path>] [--headless] [--frames N] [--timeout-s S]\n"
                "  --elf        boot ELF (default: %.*s, relative to the working directory)\n"
                "  --headless   hidden window; stop on the first missing guest function\n"
                "  --frames N   exit 0 after N presented frames (3 if the guest stops first)\n"
                "  --timeout-s S  exit 4 if S seconds pass first\n"
                "  K2_PROBE=path  load a probe spec (probes/README.md); exit 5 if invalid\n"
                "  K2_PROBE_LOG=path  probe output file (default: stderr)\n",
                static_cast<int>(kDefaultElf.size()), kDefaultElf.data());
#else
    std::printf("No recompiled game code in this build (run tools/recomp/run-recomp.sh and reconfigure).\n");
#endif
}

#if K2_HAS_GENERATED
std::optional<std::uint32_t> parse_u32(std::string_view text)
{
    std::uint32_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// Fills `out`; returns 0, or 2 for a usage error.
int parse_boot_args(int argc, char **argv, k2::app::BootOptions &out)
{
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--headless") {
            out.headless = true;
        } else if (arg == "--elf" && has_value) {
            out.elf_path = argv[++i];
        } else if ((arg == "--frames" || arg == "--timeout-s") && has_value) {
            const auto value = parse_u32(argv[++i]);
            if (!value) {
                std::fprintf(stderr, "kessen2: %s expects a non-negative integer, got '%s'\n",
                             argv[i - 1], argv[i]);
                return 2;
            }
            (arg == "--frames" ? out.frames : out.timeout_s) = *value;
        } else {
            std::fprintf(stderr, "kessen2: unknown or incomplete argument '%s'\n", argv[i]);
            print_usage();
            return 2;
        }
    }
    if (out.elf_path.empty()) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(std::filesystem::path(kDefaultElf), ec)) {
            std::fprintf(stderr, "kessen2: no boot ELF at %.*s; pass --elf <path>\n",
                         static_cast<int>(kDefaultElf.size()), kDefaultElf.data());
            return 2;
        }
        out.elf_path = std::string(kDefaultElf);
    }
    if (reject_removed_env_vars()) {
        return k2::app::kBootProbeInvalid;
    }
    if (const char *probe = std::getenv("K2_PROBE"); probe != nullptr) {
        out.probe_path = probe;
        if (out.probe_path.empty()) {
            std::fprintf(stderr, "kessen2: K2_PROBE is set but empty\n");
            return k2::app::kBootProbeInvalid;
        }
    }
    if (const char *log = std::getenv("K2_PROBE_LOG"); log != nullptr) {
        out.probe_log_path = log;
    }
    return 0;
}
#endif

} // namespace

int main(int argc, char **argv)
{
    const std::string_view arg = argc > 1 ? argv[1] : "";
    if (arg == "--version" || arg == "-v") {
        print_version();
        return 0;
    }
    if (arg == "--help" || arg == "-h") {
        print_usage();
        return 0;
    }

#if K2_HAS_GENERATED
    k2::app::BootOptions options;
    if (const int rc = parse_boot_args(argc, argv, options); rc != 0) {
        return rc;
    }
    // PS2Recomp's runtime owns the window (raylib) for now; SDL platform init is not used on
    // this path. Exit like upstream's runner: skip static/runtime teardown.
    const int rc = k2::app::boot(options);
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(rc);
#else
    if (reject_removed_env_vars()) {
        return 5;
    }
    if (std::getenv("K2_PROBE") != nullptr) {
        // Fail closed: probes need recompiled code, so a set K2_PROBE cannot be honoured here.
        std::fprintf(stderr, "kessen2: K2_PROBE is set, but this build has no recompiled game code\n");
        return 5;
    }
    if (!arg.empty()) {
        std::fprintf(stderr, "kessen2: unknown argument '%s'\n", argv[1]);
        print_usage();
        return 2;
    }
    if (!k2::platform::init()) {
        std::fprintf(stderr, "kessen2: platform init failed: %s\n", k2::platform::last_error().c_str());
        return 1;
    }
    print_usage();
    k2::platform::shutdown();
    return 0;
#endif
}

// SPDX-License-Identifier: GPL-3.0-or-later
// kessen2 — composition root. Wires platform, render, runtime extensions and (when present)
// recompiled game code together. Keep this file thin.
#include "k2/platform.h"
#include "k2/render.h"
#include "k2/runtime_ext.h"

#include <cstdio>
#include <string_view>

#ifndef K2_VERSION
#define K2_VERSION "0.0.0"
#endif

namespace {

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
    std::printf("usage: kessen2 [--version | --help]\n"
                "Phase 1 scaffold: game boot is not implemented yet.\n");
}

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
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Host platform abstraction. The only module allowed to talk to SDL3.
#pragma once

#include <string>

namespace k2::platform {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

// SDL version the binary is running against (the DLL), and the one it was built with.
Version sdl_linked_version();
Version sdl_compiled_version();
std::string to_string(const Version &v);

// Initialise / shut down the platform layer. Phase 1 initialises no subsystems.
bool init();
void shutdown();
std::string last_error();

} // namespace k2::platform

// SPDX-License-Identifier: GPL-3.0-or-later
#include "k2/platform.h"

#include <SDL3/SDL.h>

namespace k2::platform {

namespace {
Version decode(int v)
{
    return Version{SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v)};
}
} // namespace

Version sdl_linked_version() { return decode(SDL_GetVersion()); }

Version sdl_compiled_version() { return decode(SDL_VERSION); }

std::string to_string(const Version &v)
{
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

bool init() { return SDL_Init(0); }

void shutdown() { SDL_Quit(); }

std::string last_error()
{
    const char *err = SDL_GetError();
    return err ? err : "";
}

} // namespace k2::platform

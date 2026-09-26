// SPDX-License-Identifier: GPL-3.0-or-later
// Renderer stub. Will implement GSRasterBackend on top of the platform GPU context.
#include "k2/render.h"

#if K2_HAS_PS2_RUNTIME
#include "runtime/gs/gs_backend.h"
#include <type_traits>
static_assert(std::is_polymorphic_v<GSRasterBackend>, "PS2Recomp GS backend interface changed");
#endif

namespace k2::render {

const char *backend_name()
{
#if K2_HAS_PS2_RUNTIME
    return "stub (GSRasterBackend interface available)";
#else
    return "stub (built without PS2Recomp)";
#endif
}

} // namespace k2::render

// SPDX-License-Identifier: GPL-3.0-or-later
// BRINGUP.md #2: libcdvd sceCdDiskReady at 0x00114C80 was not named by Ghidra, so it was
// recompiled instead of routed to the runtime. It binds CDVDFSV SID 0x8000059A, which lives in
// the IOP ROM (not emulated), and retries forever. Route it to PS2Recomp's HLE sceCdDiskReady,
// like the other libcdvd entry points in analysis/kessen2.toml.
#include "fixes.h"
#include "hle_bind.h"

#include "ps2_stubs.h"

namespace k2::runtime_ext::fixes {

int fix_cd_disk_ready(PS2Runtime &runtime)
{
    return bind_hle<&ps2_stubs::sceCdDiskReady>(runtime, 0x00114C80u, "sceCdDiskReady");
}

} // namespace k2::runtime_ext::fixes

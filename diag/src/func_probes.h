// SPDX-License-Identifier: GPL-3.0-or-later
// Function entry/return probes (internal to k2_diag).
#pragma once

#include "k2/diag/probe_log.h"
#include "k2/diag/probe_spec.h"

#include <atomic>
#include <cstdint>
#include <expected>
#include <string>

class PS2Runtime;

namespace k2::diag::detail {

inline constexpr std::size_t kFuncSlots = Limits::kMaxFuncs;

// Wraps every spec.funcs entry via PS2Runtime::replaceFunction. `frame` and `log` must outlive
// the process' guest execution. Fails closed (nothing installed) if any address has no
// recompiled function, or if probes were already installed.
std::expected<void, std::string> install_func_probes(PS2Runtime &runtime, const ProbeSpec &spec, ProbeLog &log,
                                                     const std::atomic<std::uint32_t> &frame);

} // namespace k2::diag::detail

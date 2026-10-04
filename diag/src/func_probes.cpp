// SPDX-License-Identifier: GPL-3.0-or-later
// Function probes: each traced guest function is replaced in the runtime's function table by a
// template wrapper (one per slot) that logs entry/return and calls the original. Recompiled
// functions take no user pointer, so the per-slot state is necessarily process-global; it is
// written once by install_func_probes before the guest starts and is read-only afterwards
// except for the atomic counters.
#include "func_probes.h"

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ps2_memory.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <optional>
#include <utility>

namespace k2::diag::detail {
namespace {

struct Slot {
    std::uint32_t address = 0;
    std::uint32_t calls_window = 0;
    std::uint32_t nonzero_budget = 0;
    std::optional<std::uint32_t> word;
    PS2Runtime::RecompiledFunction original = nullptr;
    std::atomic<std::uint32_t> calls{0};   // all calls
    std::atomic<std::uint32_t> logged{0};  // calls seen while armed
    std::atomic<std::uint32_t> nonzero{0}; // nonzero returns logged
};

struct State {
    std::array<Slot, kFuncSlots> slots;
    ProbeLog *log = nullptr;
    const std::atomic<std::uint32_t> *frame = nullptr;
    std::optional<ArmProbe> arm;
    std::atomic<bool> armed{true};
    bool installed = false;
};

State &state()
{
    static State s;
    return s;
}

std::uint32_t read_word(const std::uint8_t *rdram, std::uint32_t address)
{
    const std::uint8_t *p = getConstMemPtr(rdram, address);
    std::uint32_t value = 0;
    if (p != nullptr) {
        std::memcpy(&value, p, sizeof(value));
    }
    return value;
}

void emit(const char *kind, const char *fields)
{
    State &s = state();
    s.log->write(s.frame->load(std::memory_order_relaxed), kind, fields);
}

template <std::size_t Index>
void wrapper(std::uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    State &s = state();
    Slot &t = s.slots[Index];
    char buf[256];
    const std::uint32_t total = t.calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (s.arm && !s.armed.load(std::memory_order_relaxed) && t.address == s.arm->address &&
        total >= s.arm->after) {
        if (!s.armed.exchange(true, std::memory_order_relaxed)) {
            std::snprintf(buf, sizeof(buf), "fn=0x%08x call=%u", t.address, total);
            emit("armed", buf);
        }
    }
    if (!s.armed.load(std::memory_order_relaxed)) {
        t.original(rdram, ctx, runtime);
        return;
    }
    const std::uint32_t n = t.logged.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool in_window = n <= t.calls_window;
    if (in_window) {
        int len = std::snprintf(buf, sizeof(buf),
                                "fn=0x%08x n=%u a0=0x%08x a1=0x%08x a2=0x%08x a3=0x%08x ra=0x%08x gp=0x%08x "
                                "sp=0x%08x",
                                t.address, n, GPR_U32(ctx, 4), GPR_U32(ctx, 5), GPR_U32(ctx, 6), GPR_U32(ctx, 7),
                                GPR_U32(ctx, 31), GPR_U32(ctx, 28), GPR_U32(ctx, 29));
        if (t.word) {
            std::snprintf(buf + len, sizeof(buf) - static_cast<std::size_t>(len), " word[0x%08x]=0x%08x", *t.word,
                          read_word(rdram, *t.word));
        }
        emit("call", buf);
    } else if ((n & (n - 1)) == 0) {
        std::snprintf(buf, sizeof(buf), "fn=0x%08x calls=%u", t.address, n);
        emit("calls", buf);
    }

    t.original(rdram, ctx, runtime);

    // pc != caller's ra means the function left through the scheduler (yield, wait).
    const std::uint32_t v0 = GPR_U32(ctx, 2);
    if (in_window) {
        int len = std::snprintf(buf, sizeof(buf), "fn=0x%08x n=%u pc=0x%08x v0=0x%08x", t.address, n, ctx->pc, v0);
        if (t.word) {
            std::snprintf(buf + len, sizeof(buf) - static_cast<std::size_t>(len), " word[0x%08x]=0x%08x", *t.word,
                          read_word(rdram, *t.word));
        }
        emit("ret", buf);
    } else if (v0 != 0 && t.nonzero_budget != 0 &&
               t.nonzero.fetch_add(1, std::memory_order_relaxed) < t.nonzero_budget) {
        std::snprintf(buf, sizeof(buf), "fn=0x%08x n=%u pc=0x%08x v0=0x%08x nonzero=1", t.address, n, ctx->pc, v0);
        emit("ret", buf);
    }
}

template <std::size_t... I>
constexpr std::array<PS2Runtime::RecompiledFunction, sizeof...(I)> make_wrappers(std::index_sequence<I...>)
{
    return {&wrapper<I>...};
}

constexpr auto kWrappers = make_wrappers(std::make_index_sequence<kFuncSlots>{});

} // namespace

std::expected<void, std::string> install_func_probes(PS2Runtime &runtime, const ProbeSpec &spec, ProbeLog &log,
                                                     const std::atomic<std::uint32_t> &frame)
{
    State &s = state();
    if (s.installed) {
        return std::unexpected(std::string("function probes are already installed"));
    }
    if (spec.funcs.size() > kFuncSlots) {
        return std::unexpected(std::string("too many func probes"));
    }
    // Validate every address before touching the function table (all or nothing).
    for (const auto &f : spec.funcs) {
        if (!runtime.hasFunction(f.address)) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "func 0x%08x: no recompiled function at this address", f.address);
            return std::unexpected(std::string(buf));
        }
    }
    s.log = &log;
    s.frame = &frame;
    s.arm = spec.arm;
    s.armed.store(!spec.arm.has_value(), std::memory_order_relaxed);
    for (std::size_t i = 0; i < spec.funcs.size(); ++i) {
        const auto &f = spec.funcs[i];
        Slot &slot = s.slots[i];
        slot.address = f.address;
        slot.calls_window = f.calls;
        slot.nonzero_budget = f.nonzero;
        slot.word = f.word;
        slot.original = runtime.lookupFunction(f.address);
    }
    for (std::size_t i = 0; i < spec.funcs.size(); ++i) {
        if (!runtime.replaceFunction(spec.funcs[i].address, kWrappers[i])) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "func 0x%08x: cannot replace function", spec.funcs[i].address);
            return std::unexpected(std::string(buf));
        }
    }
    s.installed = true;
    return {};
}

} // namespace k2::diag::detail

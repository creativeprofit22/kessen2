// SPDX-License-Identifier: GPL-3.0-or-later
// Bring-up diagnostic (not a fix): K2_TRACE_FUNCS=0x1b67b0,0x14d590 wraps up to kMaxTraced
// recompiled guest functions and logs their first kMaxLogged calls (a0..a2, ra, gp, sp) and
// returns (pc, v0) to stderr, then only a call count at each power-of-two call number (to measure
// call rates, e.g. a vblank handler). With K2_TRACE_NONZERO=1, calls past the first kMaxLogged
// still log a return line whenever v0 != 0 (up to kMaxNonzeroLogged in total), to catch the rare
// call that changes state. With K2_TRACE_WORD=0xADDR, each logged call/return line also shows
// the 32-bit guest word at ADDR (to find which call writes it). With K2_TRACE_ARM=0xADDR[:N] (ADDR
// must also be traced), nothing is logged until ADDR's N-th call (default 1); from then on every
// function's kMaxLogged window and the nonzero budget start fresh. Off unless the variable is set.
#include "fixes.h"

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <utility>

namespace k2::runtime_ext::fixes {
namespace {

constexpr std::size_t kMaxTraced = 8;
constexpr std::uint32_t kMaxLogged = 64;
constexpr std::uint32_t kMaxNonzeroLogged = 256;

struct Traced {
    std::uint32_t address = 0;
    PS2Runtime::RecompiledFunction original = nullptr;
    std::atomic<std::uint32_t> calls{0};
    std::atomic<std::uint32_t> logged{0}; // calls seen while armed
};

// Written once in fix_trace_funcs before the guest starts, then read-only.
std::array<Traced, kMaxTraced> g_traced;
bool g_logNonzero = false;
std::atomic<std::uint32_t> g_nonzeroLogged{0};
bool g_haveWord = false;
std::uint32_t g_wordAddress = 0;
std::uint32_t g_armAddress = 0; // 0 = armed from the start
std::uint32_t g_armCount = 1;
std::atomic<bool> g_armed{true};

std::uint32_t read_word(const std::uint8_t *rdram)
{
    if (!g_haveWord) {
        return 0;
    }
    const std::uint8_t *p = getConstMemPtr(rdram, g_wordAddress);
    std::uint32_t value = 0;
    if (p != nullptr) {
        std::memcpy(&value, p, sizeof(value));
    }
    return value;
}

template <std::size_t Slot>
void traced(std::uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    Traced &t = g_traced[Slot];
    const std::uint32_t total = t.calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!g_armed.load(std::memory_order_relaxed) && t.address == g_armAddress && total >= g_armCount) {
        std::fprintf(stderr, "[kessen2:trace] armed at 0x%08x call %u\n", t.address, total);
        g_armed.store(true, std::memory_order_relaxed);
    }
    if (!g_armed.load(std::memory_order_relaxed)) {
        t.original(rdram, ctx, runtime);
        return;
    }
    const std::uint32_t n = t.logged.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= kMaxLogged) {
        std::fprintf(stderr,
                     "[kessen2:trace] 0x%08x call#%u a0=0x%x a1=0x%x a2=0x%x ra=0x%08x gp=0x%x sp=0x%x "
                     "word=0x%x\n",
                     t.address, n, GPR_U32(ctx, 4), GPR_U32(ctx, 5), GPR_U32(ctx, 6),
                     GPR_U32(ctx, 31), GPR_U32(ctx, 28), GPR_U32(ctx, 29), read_word(rdram));
    } else if ((n & (n - 1)) == 0) {
        std::fprintf(stderr, "[kessen2:trace] 0x%08x calls=%u\n", t.address, n);
    }
    t.original(rdram, ctx, runtime);
    if (n <= kMaxLogged) {
        // pc != caller's ra means the function left through the scheduler (yield, wait, throw).
        std::fprintf(stderr, "[kessen2:trace] 0x%08x ret#%u pc=0x%08x v0=0x%x word=0x%x\n", t.address,
                     n, ctx->pc, GPR_U32(ctx, 2), read_word(rdram));
    } else if (g_logNonzero && GPR_U32(ctx, 2) != 0 &&
               g_nonzeroLogged.fetch_add(1, std::memory_order_relaxed) < kMaxNonzeroLogged) {
        std::fprintf(stderr, "[kessen2:trace] 0x%08x ret#%u pc=0x%08x v0=0x%x (nonzero)\n",
                     t.address, n, ctx->pc, GPR_U32(ctx, 2));
    }
}

template <std::size_t... Slots>
constexpr std::array<PS2Runtime::RecompiledFunction, sizeof...(Slots)>
make_wrappers(std::index_sequence<Slots...>)
{
    return {&traced<Slots>...};
}

constexpr auto kWrappers = make_wrappers(std::make_index_sequence<kMaxTraced>{});

} // namespace

int fix_trace_funcs(PS2Runtime &runtime)
{
    const char *env = std::getenv("K2_TRACE_FUNCS");
    if (env == nullptr) {
        return 0;
    }
    const char *nonzero = std::getenv("K2_TRACE_NONZERO");
    g_logNonzero = nonzero != nullptr && std::string_view(nonzero) == "1";
    if (const char *arm = std::getenv("K2_TRACE_ARM"); arm != nullptr) {
        std::string_view text(arm);
        std::string_view count;
        if (const std::size_t colon = text.find(':'); colon != std::string_view::npos) {
            count = text.substr(colon + 1);
            text = text.substr(0, colon);
        }
        if (text.starts_with("0x") || text.starts_with("0X")) {
            text.remove_prefix(2);
        }
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), g_armAddress, 16);
        if (ec == std::errc{} && end == text.data() + text.size() && g_armAddress != 0) {
            if (!count.empty()) {
                std::from_chars(count.data(), count.data() + count.size(), g_armCount, 10);
            }
            g_armed.store(false, std::memory_order_relaxed);
        } else {
            g_armAddress = 0;
        }
    }
    if (const char *word = std::getenv("K2_TRACE_WORD"); word != nullptr) {
        std::string_view text(word);
        if (text.starts_with("0x") || text.starts_with("0X")) {
            text.remove_prefix(2);
        }
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), g_wordAddress, 16);
        g_haveWord = ec == std::errc{} && end == text.data() + text.size() && (g_wordAddress & 3u) == 0;
    }
    std::string_view list(env);
    std::size_t used = 0;
    while (!list.empty() && used < kMaxTraced) {
        const std::size_t comma = list.find(',');
        std::string_view item = list.substr(0, comma);
        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
        if (item.starts_with("0x") || item.starts_with("0X")) {
            item.remove_prefix(2);
        }
        std::uint32_t address = 0;
        const auto [end, ec] = std::from_chars(item.data(), item.data() + item.size(), address, 16);
        if (ec != std::errc{} || end != item.data() + item.size()) {
            std::fprintf(stderr, "[kessen2:trace] ignoring bad address '%.*s'\n",
                         static_cast<int>(item.size()), item.data());
            continue;
        }
        const auto original = runtime.lookupFunction(address);
        if (!runtime.hasFunction(address) || original == nullptr) {
            std::fprintf(stderr, "[kessen2:trace] no recompiled function at 0x%08x\n", address);
            continue;
        }
        g_traced[used].address = address;
        g_traced[used].original = original;
        runtime.replaceFunction(address, kWrappers[used]);
        std::fprintf(stderr, "[kessen2:trace] tracing 0x%08x\n", address);
        ++used;
    }
    return used > 0 ? 1 : 0;
}

} // namespace k2::runtime_ext::fixes

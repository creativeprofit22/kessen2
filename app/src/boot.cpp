// SPDX-License-Identifier: GPL-3.0-or-later
#include "boot.h"

#include "ps2_runtime.h"
#include "raylib.h"
#include "rlgl.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_memory.h"

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>

namespace k2::app {
namespace {

using Clock = std::chrono::steady_clock;

// Extra time the runtime gets to join its game thread after a watchdog stop request before the
// process is killed (a guest spinning without yielding never observes requestStop()).
constexpr auto kStopGrace = std::chrono::seconds(10);

// Bring-up diagnostic: K2_WATCH=0xADDR[,0xADDR...] prints up to kMaxWatched 32-bit guest RAM
// words every K2_WATCH_EVERY presented frames (default kWatchInterval), e.g. a movie frame
// counter. Read-only.
constexpr std::size_t kMaxWatched = 8;
constexpr std::uint32_t kWatchInterval = 600;

struct WatchList {
    std::array<std::uint32_t, kMaxWatched> addresses{};
    std::size_t count = 0;
};

WatchList parse_watch_list(const char *env)
{
    WatchList out;
    std::string_view list = env != nullptr ? std::string_view(env) : std::string_view{};
    while (!list.empty() && out.count < kMaxWatched) {
        const std::size_t comma = list.find(',');
        std::string_view item = list.substr(0, comma);
        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
        if (item.starts_with("0x") || item.starts_with("0X")) {
            item.remove_prefix(2);
        }
        std::uint32_t address = 0;
        const auto [end, ec] = std::from_chars(item.data(), item.data() + item.size(), address, 16);
        if (ec != std::errc{} || end != item.data() + item.size() || (address & 3u) != 0) {
            std::fprintf(stderr, "[kessen2:watch] ignoring bad address '%.*s'\n",
                         static_cast<int>(item.size()), item.data());
            continue;
        }
        out.addresses[out.count++] = address;
    }
    return out;
}

struct FrameCounter {
    std::uint32_t target = 0;
    std::atomic<std::uint32_t> presented{0};
    std::atomic<bool> reached{false};
    WatchList watch;
    // Bring-up diagnostic: K2_SCREENSHOT_EVERY=N saves k2-frame-NNNNNN.png to the working
    // directory every N presented frames (0 = off).
    std::uint32_t screenshotEvery = 0;
    std::uint32_t watchEvery = kWatchInterval;
};

std::uint32_t parse_positive(const char *env)
{
    if (env == nullptr) {
        return 0;
    }
    const std::string_view text(env);
    std::uint32_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    return (ec == std::errc{} && end == text.data() + text.size()) ? value : 0;
}

void print_watch(PS2Runtime &runtime, const WatchList &watch, std::uint32_t frame)
{
    const std::uint8_t *rdram = runtime.memory().getRDRAM();
    for (std::size_t i = 0; i < watch.count; ++i) {
        const std::uint8_t *p = getConstMemPtr(rdram, watch.addresses[i]);
        std::uint32_t value = 0;
        if (p != nullptr) {
            std::memcpy(&value, p, sizeof(value));
        }
        std::fprintf(stderr, "[kessen2:watch] frame=%u 0x%08x=0x%08x (%u)\n", frame,
                     watch.addresses[i], value, value);
    }
}

void on_frame(PS2Runtime &runtime, void *user)
{
    auto &counter = *static_cast<FrameCounter *>(user);
    const std::uint32_t n = counter.presented.fetch_add(1, std::memory_order_relaxed) + 1;
    if (counter.watch.count != 0 && n % counter.watchEvery == 0) {
        print_watch(runtime, counter.watch, n);
    }
    if (counter.screenshotEvery != 0 && n % counter.screenshotEvery == 0) {
        // Runs on the render thread before EndDrawing: flush raylib's batched draws so the
        // frame texture is actually in the back buffer, then read it.
        rlDrawRenderBatchActive();
        TakeScreenshot(TextFormat("k2-frame-%06u.png", n));
    }
    if (counter.target != 0 && n >= counter.target && !counter.reached.exchange(true)) {
        runtime.requestStop();
    }
}

void no_op(PS2Runtime &, void *) {}

// Requests a stop after `timeout`, then kills the process if run() still hasn't returned.
class Watchdog {
public:
    Watchdog(PS2Runtime &runtime, std::chrono::seconds timeout, const FrameCounter &frames)
    {
        if (timeout.count() == 0) {
            return;
        }
        thread_ = std::thread([this, &runtime, timeout, &frames] {
            std::unique_lock lock(mutex_);
            if (cv_.wait_for(lock, timeout, [this] { return done_; })) {
                return;
            }
            fired_ = true;
            std::fprintf(stderr, "[kessen2] boot watchdog: timeout after %lld s (%u frames)\n",
                         static_cast<long long>(timeout.count()), frames.presented.load());
            runtime.requestStop();
            if (!cv_.wait_for(lock, kStopGrace, [this] { return done_; })) {
                std::fprintf(stderr, "[kessen2] boot watchdog: runtime did not stop, killing\n");
                std::fflush(stdout);
                std::fflush(stderr);
                std::_Exit(kBootTimeout);
            }
        });
    }
    Watchdog(const Watchdog &) = delete;
    Watchdog &operator=(const Watchdog &) = delete;
    ~Watchdog()
    {
        {
            std::lock_guard lock(mutex_);
            done_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    bool fired() const
    {
        std::lock_guard lock(mutex_);
        return fired_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;
    bool fired_ = false;
    std::thread thread_;
};

} // namespace

int boot(const BootOptions &options)
{
    // Owned for the rest of the process: see boot.h (the caller exits with std::_Exit).
    auto runtime = std::make_unique<PS2Runtime>();
    FrameCounter frames;
    frames.target = options.frames;
    frames.watch = parse_watch_list(std::getenv("K2_WATCH"));
    frames.screenshotEvery = parse_positive(std::getenv("K2_SCREENSHOT_EVERY"));
    if (const std::uint32_t every = parse_positive(std::getenv("K2_WATCH_EVERY")); every != 0) {
        frames.watchEvery = every;
    }
    runtime->setDebugUiCallbacks(no_op, on_frame, no_op, &frames);

    if (options.headless) {
        // raylib ORs config flags, so this survives the runtime's own SetConfigFlags call.
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        runtime->setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::Stop);
    }

    if (!runtime->initialize("Kessen II")) {
        std::fprintf(stderr, "[kessen2] boot: runtime initialize failed\n");
        return kBootInitFailed;
    }
    if (!runtime->loadELF(options.elf_path)) {
        std::fprintf(stderr, "[kessen2] boot: failed to load ELF '%s'\n", options.elf_path.c_str());
        return kBootInitFailed;
    }

    std::fprintf(stderr, "[kessen2] boot: elf=%s headless=%d frames=%u timeout_s=%u\n",
                 options.elf_path.c_str(), options.headless ? 1 : 0, options.frames, options.timeout_s);
    const auto start = Clock::now();
    bool timed_out = false;
    {
        Watchdog watchdog(*runtime, std::chrono::seconds(options.timeout_s), frames);
        runtime->run();
        timed_out = watchdog.fired();
    }
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();

    int code = kBootOk;
    const char *result = "ok";
    if (frames.reached.load()) {
        code = kBootOk;
    } else if (timed_out) {
        code = kBootTimeout;
        result = "timeout";
    } else if (options.frames != 0) {
        code = kBootStoppedEarly;
        result = "stopped-early";
    }
    // Guest vsync count: read after run() returned, so the scheduler is no longer running.
    const unsigned long long vsync = runtime->eeScheduler().currentVSyncTick();
    std::fprintf(stderr, "[kessen2] boot: result=%s frames=%u vsync=%llu elapsed_ms=%lld\n", result,
                 frames.presented.load(), vsync, static_cast<long long>(elapsed_ms));
    (void)runtime.release(); // deliberately leaked, see above
    return code;
}

} // namespace k2::app

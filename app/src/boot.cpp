// SPDX-License-Identifier: GPL-3.0-or-later
#include "boot.h"

#include "ps2_runtime.h"
#include "raylib.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

namespace k2::app {
namespace {

using Clock = std::chrono::steady_clock;

// Extra time the runtime gets to join its game thread after a watchdog stop request before the
// process is killed (a guest spinning without yielding never observes requestStop()).
constexpr auto kStopGrace = std::chrono::seconds(10);

struct FrameCounter {
    std::uint32_t target = 0;
    std::atomic<std::uint32_t> presented{0};
    std::atomic<bool> reached{false};
};

void on_frame(PS2Runtime &runtime, void *user)
{
    auto &counter = *static_cast<FrameCounter *>(user);
    const std::uint32_t n = counter.presented.fetch_add(1, std::memory_order_relaxed) + 1;
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
    std::fprintf(stderr, "[kessen2] boot: result=%s frames=%u elapsed_ms=%lld\n", result,
                 frames.presented.load(), static_cast<long long>(elapsed_ms));
    (void)runtime.release(); // deliberately leaked, see above
    return code;
}

} // namespace k2::app

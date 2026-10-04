// SPDX-License-Identifier: GPL-3.0-or-later
#include "boot.h"

#include "k2/diag/probe_log.h"
#include "k2/diag/probe_session.h"
#include "k2/diag/probe_spec.h"
#include "ps2_runtime.h"
#include "raylib.h"
#include "rlgl.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_memory.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

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
    // Probe diagnostics (K2_PROBE, ADR-0006); null when no probe is loaded. Set before run().
    k2::diag::ProbeSession *probe = nullptr;
};

void on_frame(PS2Runtime &runtime, void *user)
{
    auto &counter = *static_cast<FrameCounter *>(user);
    const std::uint32_t n = counter.presented.fetch_add(1, std::memory_order_relaxed) + 1;
    if (counter.probe != nullptr && counter.probe->on_presented_frame(runtime, n).screenshot) {
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
    // Parse and validate the probe spec before anything else: a bad spec must stop the run.
    std::optional<k2::diag::ProbeSpec> probe_spec;
    std::unique_ptr<k2::diag::ProbeLog> probe_log;
    if (!options.probe_path.empty()) {
        auto spec = k2::diag::load_probe_file(options.probe_path);
        if (!spec) {
            std::fprintf(stderr, "[kessen2] probe: %s: %s\n", options.probe_path.c_str(),
                         k2::diag::to_string(spec.error()).c_str());
            return kBootProbeInvalid;
        }
        auto log = k2::diag::ProbeLog::open(options.probe_log_path);
        if (!log) {
            std::fprintf(stderr, "[kessen2] probe: %s\n", log.error().c_str());
            return kBootProbeInvalid;
        }
        probe_spec = std::move(*spec);
        probe_log = std::move(*log);
    }

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

    // Installed after loadELF, so function probes wrap whatever the game override installed.
    std::unique_ptr<k2::diag::ProbeSession> probe;
    if (probe_spec) {
        auto session = k2::diag::ProbeSession::install(*runtime, *probe_spec, *probe_log);
        if (!session) {
            std::fprintf(stderr, "[kessen2] probe: %s: %s\n", options.probe_path.c_str(), session.error().c_str());
            return kBootProbeInvalid;
        }
        probe = std::move(*session);
        frames.probe = probe.get();
        std::fprintf(stderr, "[kessen2] probe: loaded %s\n", options.probe_path.c_str());
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
    if (probe) {
        probe->finish();
        // Leaked like the runtime (see boot.h): nothing may outlive them on a late callback.
        (void)probe.release();
        (void)probe_log.release();
    }

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

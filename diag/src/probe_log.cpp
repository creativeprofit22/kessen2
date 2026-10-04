// SPDX-License-Identifier: GPL-3.0-or-later
#include "k2/diag/probe_log.h"

#include <utility>

#if defined(_MSC_VER)
#include <share.h>
#endif

namespace k2::diag {

ProbeLog::ProbeLog(Sink sink) : sink_(std::move(sink)) {}

std::expected<std::unique_ptr<ProbeLog>, std::string> ProbeLog::open(const std::filesystem::path &path)
{
    if (path.empty()) {
        return std::make_unique<ProbeLog>([](std::string_view line) {
            std::fprintf(stderr, "[k2probe] %.*s", static_cast<int>(line.size()), line.data());
        });
    }
    std::FILE *raw = nullptr;
#if defined(_MSC_VER)
    // _wfsopen (not _wfopen_s, which is exclusive) so the log can be tailed while the game runs.
    raw = _wfsopen(path.c_str(), L"wb", _SH_DENYWR);
#else
    raw = std::fopen(path.c_str(), "wb");
#endif
    if (raw == nullptr) {
        return std::unexpected("cannot open probe log: " + path.string());
    }
    std::shared_ptr<std::FILE> file(raw, [](std::FILE *f) { std::fclose(f); });
    // Flush every line: the boot watchdog ends hangs with std::_Exit and crashes skip stdio
    // teardown, so anything still buffered would be lost exactly when it matters most.
    auto log = std::make_unique<ProbeLog>([file](std::string_view line) {
        std::fwrite(line.data(), 1, line.size(), file.get());
        std::fflush(file.get());
    });
    log->file_ = std::move(file);
    return log;
}

void ProbeLog::write(std::uint32_t frame, std::string_view kind, std::string_view fields)
{
    const std::lock_guard lock(mutex_);
    std::string line = "frame=" + std::to_string(frame) + " seq=" + std::to_string(seq_++) + " kind=";
    line += kind;
    if (!fields.empty()) {
        line += ' ';
        line += fields;
    }
    line += '\n';
    sink_(line);
}

void ProbeLog::flush()
{
    const std::lock_guard lock(mutex_);
    if (file_) {
        std::fflush(file_.get());
    } else {
        std::fflush(stderr);
    }
}

std::string format_write_event(const WriteEvent &event)
{
    char buf[160];
    int n = std::snprintf(buf, sizeof(buf), "addr=0x%08x old=0x%02x new=0x%02x ", event.address,
                          event.old_value, event.new_value);
    if (event.writer) {
        n += std::snprintf(buf + n, sizeof(buf) - static_cast<std::size_t>(n), "writer=0x%08x", *event.writer);
    } else {
        n += std::snprintf(buf + n, sizeof(buf) - static_cast<std::size_t>(n), "writer=unattributed");
    }
    if (event.edge == Edge::Enter) {
        std::snprintf(buf + n, sizeof(buf) - static_cast<std::size_t>(n), " site=0x%08x edge=enter", event.site);
    } else {
        std::snprintf(buf + n, sizeof(buf) - static_cast<std::size_t>(n), " edge=exit");
    }
    return buf;
}

} // namespace k2::diag

// SPDX-License-Identifier: GPL-3.0-or-later
#include "k2/diag/probe_spec.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <map>
#include <ranges>
#include <set>
#include <span>
#include <system_error>
#include <utility>

namespace k2::diag {
namespace {

using Result = std::expected<void, std::string>;

std::string_view trim(std::string_view s)
{
    constexpr std::string_view kSpace = " \t\r";
    const auto first = s.find_first_not_of(kSpace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(kSpace);
    return s.substr(first, last - first + 1);
}

std::vector<std::string_view> split_ws(std::string_view s)
{
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
            ++i;
        }
        const std::size_t start = i;
        while (i < s.size() && s[i] != ' ' && s[i] != '\t') {
            ++i;
        }
        if (i > start) {
            out.push_back(s.substr(start, i - start));
        }
    }
    return out;
}

std::string quoted(std::string_view s)
{
    return "'" + std::string(s) + "'";
}

// Hex with optional 0x/0X prefix, 1..8 digits.
std::expected<std::uint32_t, std::string> parse_hex(std::string_view text, std::string_view what)
{
    std::string_view digits = text;
    if (digits.size() >= 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
        digits.remove_prefix(2);
    }
    if (digits.empty() || digits.size() > 8) {
        return std::unexpected(std::string(what) + " must be 1-8 hex digits, got " + quoted(text));
    }
    std::uint32_t value = 0;
    for (const char c : digits) {
        std::uint32_t nibble = 0;
        if (c >= '0' && c <= '9') {
            nibble = static_cast<std::uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            nibble = static_cast<std::uint32_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            nibble = static_cast<std::uint32_t>(c - 'A' + 10);
        } else {
            return std::unexpected(std::string(what) + " is not hex: " + quoted(text));
        }
        value = (value << 4) | nibble;
    }
    return value;
}

// Decimal, digits only, inside [lo, hi].
std::expected<std::uint32_t, std::string> parse_count(std::string_view text, std::string_view what,
                                                      std::uint32_t lo, std::uint32_t hi)
{
    if (text.empty() || text.size() > 10) {
        return std::unexpected(std::string(what) + " must be a decimal number, got " + quoted(text));
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return std::unexpected(std::string(what) + " must be a decimal number, got " + quoted(text));
        }
        value = value * 10u + static_cast<std::uint64_t>(c - '0');
    }
    if (value < lo || value > hi) {
        return std::unexpected(std::string(what) + " must be in " + std::to_string(lo) + ".." +
                               std::to_string(hi) + ", got " + quoted(text));
    }
    return static_cast<std::uint32_t>(value);
}

std::expected<std::uint32_t, std::string> parse_address(std::string_view text, std::string_view what,
                                                        std::uint32_t size, bool aligned)
{
    auto value = parse_hex(text, what);
    if (!value) {
        return value;
    }
    const std::uint64_t end = static_cast<std::uint64_t>(*value) + size;
    if (*value < Limits::kRamBase || end > Limits::kRamEnd) {
        return std::unexpected(std::string(what) + " " + quoted(text) +
                               " is outside EE RAM 0x00000000-0x01FFFFFF");
    }
    if (aligned && (*value & 3u) != 0u) {
        return std::unexpected(std::string(what) + " " + quoted(text) + " is not 4-byte aligned");
    }
    return *value;
}

// ADDR:LEN, both hex.
std::expected<Range, std::string> parse_range(std::string_view text, std::string_view what,
                                              bool word_granular, std::uint32_t max_len)
{
    const auto colon = text.find(':');
    if (colon == std::string_view::npos) {
        return std::unexpected(std::string(what) + " must be ADDR:LEN (hex), got " + quoted(text));
    }
    const auto len = parse_hex(text.substr(colon + 1), std::string(what) + " length");
    if (!len) {
        return std::unexpected(len.error());
    }
    if (*len == 0 || *len > max_len) {
        return std::unexpected(std::string(what) + " length must be 1.." + std::to_string(max_len) +
                               " bytes, got " + quoted(text.substr(colon + 1)));
    }
    if (word_granular && (*len % 4u) != 0u) {
        return std::unexpected(std::string(what) + " length must be a multiple of 4, got " +
                               quoted(text.substr(colon + 1)));
    }
    const auto addr = parse_address(text.substr(0, colon), what, *len, word_granular);
    if (!addr) {
        return std::unexpected(addr.error());
    }
    return Range{*addr, *len};
}

// key=value options; every key at most once, only keys from `allowed`.
class Options {
public:
    static std::expected<Options, std::string> parse(std::span<const std::string_view> tokens,
                                                     std::initializer_list<std::string_view> allowed)
    {
        Options out;
        for (const auto token : tokens) {
            const auto eq = token.find('=');
            if (eq == std::string_view::npos || eq == 0) {
                return std::unexpected("expected key=value, got " + quoted(token));
            }
            const auto key = token.substr(0, eq);
            if (std::ranges::find(allowed, key) == allowed.end()) {
                return std::unexpected("unknown option " + quoted(key));
            }
            if (out.values_.contains(std::string(key))) {
                return std::unexpected("option " + quoted(key) + " given twice");
            }
            out.values_.emplace(std::string(key), std::string(token.substr(eq + 1)));
        }
        return out;
    }

    [[nodiscard]] const std::string *get(std::string_view key) const
    {
        const auto it = values_.find(std::string(key));
        return it == values_.end() ? nullptr : &it->second;
    }

private:
    std::map<std::string, std::string, std::less<>> values_;
};

struct Parser {
    ProbeSpec spec;
    std::set<std::uint32_t> func_addresses;
    std::size_t arm_line = 0;
    bool have_version = false;
    bool have_max_events = false;
    std::uint32_t watch_bytes = 0;
    std::uint32_t attrib_bytes = 0;

    Result directive(std::string_view name, std::span<const std::string_view> args);
    Result func(std::span<const std::string_view> args);
    Result arm(std::span<const std::string_view> args);
    Result watch(std::span<const std::string_view> args);
    Result attrib(std::span<const std::string_view> args);
    Result max_events(std::span<const std::string_view> args);
    Result screenshot(std::span<const std::string_view> args);
    Result gsregs(std::span<const std::string_view> args);
    Result gsevents(std::span<const std::string_view> args);
};

// every=N (required, >= 1) and optional from=F, shared by frame-scheduled directives.
std::expected<std::pair<std::uint32_t, std::uint32_t>, std::string>
parse_schedule(std::span<const std::string_view> args, std::string_view name)
{
    const auto opts = Options::parse(args, {"every", "from"});
    if (!opts) {
        return std::unexpected(opts.error());
    }
    const auto *every = opts->get("every");
    if (every == nullptr) {
        return std::unexpected(std::string(name) + " needs every=N");
    }
    const auto n = parse_count(*every, "every", 1, 0xFFFFFFFFu);
    if (!n) {
        return std::unexpected(n.error());
    }
    std::uint32_t from = 0;
    if (const auto *v = opts->get("from")) {
        const auto f = parse_count(*v, "from", 0, 0xFFFFFFFFu);
        if (!f) {
            return std::unexpected(f.error());
        }
        from = *f;
    }
    return std::pair{*n, from};
}

Result Parser::directive(std::string_view name, std::span<const std::string_view> args)
{
    if (!have_version) {
        if (name != "k2probe") {
            return std::unexpected("first directive must be 'k2probe 1', got " + quoted(name));
        }
        if (args.size() != 1 || args[0] != "1") {
            return std::unexpected("unsupported probe spec version (expected 'k2probe 1')");
        }
        have_version = true;
        return {};
    }
    if (name == "k2probe") {
        return std::unexpected("'k2probe' given twice");
    }
    if (name == "func") {
        return func(args);
    }
    if (name == "arm") {
        return arm(args);
    }
    if (name == "watch") {
        return watch(args);
    }
    if (name == "attrib") {
        return attrib(args);
    }
    if (name == "attrib-max-events") {
        return max_events(args);
    }
    if (name == "screenshot") {
        return screenshot(args);
    }
    if (name == "gsregs") {
        return gsregs(args);
    }
    if (name == "gsevents") {
        return gsevents(args);
    }
    return std::unexpected("unknown directive " + quoted(name));
}

Result Parser::func(std::span<const std::string_view> args)
{
    if (args.empty()) {
        return std::unexpected("func needs an address");
    }
    if (spec.funcs.size() >= Limits::kMaxFuncs) {
        return std::unexpected("too many func probes (max " + std::to_string(Limits::kMaxFuncs) + ")");
    }
    const auto addr = parse_address(args[0], "func address", 4, true);
    if (!addr) {
        return std::unexpected(addr.error());
    }
    if (!func_addresses.insert(*addr).second) {
        return std::unexpected("func " + quoted(args[0]) + " given twice");
    }
    const auto opts = Options::parse(args.subspan(1), {"calls", "nonzero", "word"});
    if (!opts) {
        return std::unexpected(opts.error());
    }
    FuncProbe probe{.address = *addr};
    if (const auto *v = opts->get("calls")) {
        const auto n = parse_count(*v, "calls", 1, Limits::kMaxCalls);
        if (!n) {
            return std::unexpected(n.error());
        }
        probe.calls = *n;
    }
    if (const auto *v = opts->get("nonzero")) {
        const auto n = parse_count(*v, "nonzero", 0, Limits::kMaxNonzero);
        if (!n) {
            return std::unexpected(n.error());
        }
        probe.nonzero = *n;
    }
    if (const auto *v = opts->get("word")) {
        const auto w = parse_address(*v, "word address", 4, true);
        if (!w) {
            return std::unexpected(w.error());
        }
        probe.word = *w;
    }
    spec.funcs.push_back(probe);
    return {};
}

Result Parser::arm(std::span<const std::string_view> args)
{
    if (spec.arm) {
        return std::unexpected("only one arm directive is allowed");
    }
    if (args.empty()) {
        return std::unexpected("arm needs a func address");
    }
    const auto addr = parse_address(args[0], "arm address", 4, true);
    if (!addr) {
        return std::unexpected(addr.error());
    }
    const auto opts = Options::parse(args.subspan(1), {"after"});
    if (!opts) {
        return std::unexpected(opts.error());
    }
    ArmProbe probe{.address = *addr};
    if (const auto *v = opts->get("after")) {
        const auto n = parse_count(*v, "after", 1, 0xFFFFFFFFu);
        if (!n) {
            return std::unexpected(n.error());
        }
        probe.after = *n;
    }
    spec.arm = probe;
    return {};
}

Result Parser::watch(std::span<const std::string_view> args)
{
    if (args.empty()) {
        return std::unexpected("watch needs ADDR:LEN");
    }
    if (spec.watches.size() >= Limits::kMaxWatchRanges) {
        return std::unexpected("too many watch ranges (max " + std::to_string(Limits::kMaxWatchRanges) + ")");
    }
    const auto range = parse_range(args[0], "watch", true, Limits::kMaxWatchRangeBytes);
    if (!range) {
        return std::unexpected(range.error());
    }
    if (watch_bytes + range->length > Limits::kMaxWatchBytes) {
        return std::unexpected("watch ranges exceed " + std::to_string(Limits::kMaxWatchBytes) + " bytes in total");
    }
    const auto opts = Options::parse(args.subspan(1), {"every"});
    if (!opts) {
        return std::unexpected(opts.error());
    }
    WatchProbe probe{.range = *range};
    if (const auto *v = opts->get("every")) {
        const auto n = parse_count(*v, "every", 1, 0xFFFFFFFFu);
        if (!n) {
            return std::unexpected(n.error());
        }
        probe.every = *n;
    }
    watch_bytes += range->length;
    spec.watches.push_back(probe);
    return {};
}

Result Parser::attrib(std::span<const std::string_view> args)
{
    if (args.size() != 1) {
        return std::unexpected("attrib takes exactly one ADDR:LEN");
    }
    if (spec.attrib.size() >= Limits::kMaxAttribRanges) {
        return std::unexpected("too many attrib ranges (max " + std::to_string(Limits::kMaxAttribRanges) + ")");
    }
    const auto range = parse_range(args[0], "attrib", false, Limits::kMaxAttribBytes);
    if (!range) {
        return std::unexpected(range.error());
    }
    if (attrib_bytes + range->length > Limits::kMaxAttribBytes) {
        return std::unexpected("attrib ranges exceed " + std::to_string(Limits::kMaxAttribBytes) + " bytes in total");
    }
    attrib_bytes += range->length;
    spec.attrib.push_back(*range);
    return {};
}

Result Parser::max_events(std::span<const std::string_view> args)
{
    if (have_max_events) {
        return std::unexpected("'attrib-max-events' given twice");
    }
    if (args.size() != 1) {
        return std::unexpected("attrib-max-events takes exactly one number");
    }
    const auto n = parse_count(args[0], "attrib-max-events", 1, Limits::kMaxAttribEvents);
    if (!n) {
        return std::unexpected(n.error());
    }
    have_max_events = true;
    spec.attrib_max_events = *n;
    return {};
}

Result Parser::screenshot(std::span<const std::string_view> args)
{
    if (spec.screenshot) {
        return std::unexpected("only one screenshot directive is allowed");
    }
    const auto schedule = parse_schedule(args, "screenshot");
    if (!schedule) {
        return std::unexpected(schedule.error());
    }
    spec.screenshot = ScreenshotProbe{.every = schedule->first, .from = schedule->second};
    return {};
}

Result Parser::gsregs(std::span<const std::string_view> args)
{
    if (spec.gsregs) {
        return std::unexpected("only one gsregs directive is allowed");
    }
    const auto schedule = parse_schedule(args, "gsregs");
    if (!schedule) {
        return std::unexpected(schedule.error());
    }
    spec.gsregs = GsRegsProbe{.every = schedule->first, .from = schedule->second};
    return {};
}

Result Parser::gsevents(std::span<const std::string_view> args)
{
    if (spec.gsevents) {
        return std::unexpected("only one gsevents directive is allowed");
    }
    const auto schedule = parse_schedule(args, "gsevents");
    if (!schedule) {
        return std::unexpected(schedule.error());
    }
    spec.gsevents = GsEventsProbe{.every = schedule->first, .from = schedule->second};
    return {};
}

// Sorted ranges must not overlap; returns the first offending pair.
std::optional<std::string> find_overlap(std::vector<Range> ranges, std::string_view kind)
{
    std::ranges::sort(ranges, {}, &Range::address);
    for (std::size_t i = 1; i < ranges.size(); ++i) {
        if (ranges[i].address < ranges[i - 1].end()) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%.*s ranges overlap: 0x%x:%x and 0x%x:%x",
                          static_cast<int>(kind.size()), kind.data(), ranges[i - 1].address,
                          ranges[i - 1].length, ranges[i].address, ranges[i].length);
            return std::string(buf);
        }
    }
    return std::nullopt;
}

} // namespace

std::string to_string(const SpecError &error)
{
    if (error.line == 0) {
        return error.message;
    }
    return "line " + std::to_string(error.line) + ": " + error.message;
}

std::expected<ProbeSpec, SpecError> parse_probe_spec(std::string_view text)
{
    if (text.size() > Limits::kMaxFileBytes) {
        return std::unexpected(SpecError{0, "probe spec is larger than 64 KiB"});
    }
    Parser parser;
    std::size_t line_no = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto nl = text.find('\n', pos);
        const auto raw = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        ++line_no;

        for (const char c : raw) {
            const auto u = static_cast<unsigned char>(c);
            if ((u < 0x20 && c != '\t' && c != '\r') || u >= 0x7F) {
                return std::unexpected(SpecError{line_no, "non-ASCII or control character"});
            }
        }
        std::string_view line = raw;
        if (const auto hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const auto tokens = split_ws(line);
        const std::span<const std::string_view> args(tokens.data() + 1, tokens.size() - 1);
        if (auto ok = parser.directive(tokens[0], args); !ok) {
            return std::unexpected(SpecError{line_no, std::move(ok.error())});
        }
        if (tokens[0] == "arm") {
            parser.arm_line = line_no;
        }
    }

    if (!parser.have_version) {
        return std::unexpected(SpecError{0, "missing 'k2probe 1' version line"});
    }
    ProbeSpec &spec = parser.spec;
    if (spec.arm && !parser.func_addresses.contains(spec.arm->address)) {
        return std::unexpected(SpecError{parser.arm_line, "arm address must also be a func probe"});
    }
    std::vector<Range> watch_ranges;
    for (const auto &w : spec.watches) {
        watch_ranges.push_back(w.range);
    }
    if (auto overlap = find_overlap(watch_ranges, "watch")) {
        return std::unexpected(SpecError{0, *overlap});
    }
    if (auto overlap = find_overlap(spec.attrib, "attrib")) {
        return std::unexpected(SpecError{0, *overlap});
    }
    if (spec.empty()) {
        return std::unexpected(SpecError{0, "probe spec has no probes"});
    }
    std::ranges::sort(spec.watches, {}, [](const WatchProbe &w) { return w.range.address; });
    std::ranges::sort(spec.attrib, {}, &Range::address);
    return std::move(spec);
}

std::expected<ProbeSpec, SpecError> load_probe_file(const std::filesystem::path &path)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::unexpected(SpecError{0, "probe file not found: " + path.string()});
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return std::unexpected(SpecError{0, "cannot stat probe file: " + path.string()});
    }
    if (size > Limits::kMaxFileBytes) {
        return std::unexpected(SpecError{0, "probe file is larger than 64 KiB: " + path.string()});
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::unexpected(SpecError{0, "cannot open probe file: " + path.string()});
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (static_cast<std::uintmax_t>(in.gcount()) != size) {
        return std::unexpected(SpecError{0, "cannot read probe file: " + path.string()});
    }
    return parse_probe_spec(text);
}

} // namespace k2::diag

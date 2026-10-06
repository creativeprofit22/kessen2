// SPDX-License-Identifier: GPL-3.0-or-later
// Probe spec parser: table-driven valid and malformed synthetic specs.
#include "check.hpp"

#include "k2/diag/probe_spec.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using k2::diag::Limits;
using k2::diag::parse_probe_spec;
using k2diagtest::check;

namespace {

std::string spec(std::string_view body)
{
    return "k2probe 1\n" + std::string(body);
}

std::string repeat_lines(int n, const char *fmt, unsigned step)
{
    std::string out;
    for (int i = 0; i < n; ++i) {
        char line[96];
        std::snprintf(line, sizeof(line), fmt, 0x1000u + static_cast<unsigned>(i) * step);
        out += line;
        out += '\n';
    }
    return out;
}

void valid_full_spec()
{
    const auto r = parse_probe_spec("# comment\n\n"
                                    "k2probe 1   # version\n"
                                    "func 0x1b6670 calls=64 nonzero=256 word=0x11ef480\n"
                                    "func 1085D0\n"
                                    "arm 0x1085d0 after=2\n"
                                    "watch 0x2000:8 every=600\n"
                                    "watch 0x1000:4\n"
                                    "attrib 0x1595:34\r\n"
                                    "attrib-max-events 100000\n"
                                    "screenshot every=150 from=30\n"
                                    "gsregs every=600 from=1200\n"
                                    "gsevents every=1 from=2400\n");
    if (!check(r.has_value(), "full spec parses")) {
        std::fprintf(stderr, "  error: %s\n", k2::diag::to_string(r.error()).c_str());
        return;
    }
    check(r->funcs.size() == 2, "two funcs");
    check(r->funcs[0].address == 0x1b6670 && r->funcs[0].calls == 64 && r->funcs[0].nonzero == 256,
          "func options");
    check(r->funcs[0].word == 0x11ef480u, "func word");
    check(r->funcs[1].address == 0x1085d0 && r->funcs[1].calls == Limits::kDefaultCalls &&
              !r->funcs[1].word,
          "func defaults, uppercase hex without 0x");
    check(r->arm && r->arm->address == 0x1085d0 && r->arm->after == 2, "arm");
    check(r->watches.size() == 2 && r->watches[0].range.address == 0x1000 &&
              r->watches[0].every == Limits::kDefaultWatchEvery && r->watches[1].range.length == 8,
          "watches sorted by address with defaults");
    check(r->attrib.size() == 1 && r->attrib[0].address == 0x1595 && r->attrib[0].length == 0x34,
          "attrib length is hex");
    check(r->attrib_max_events == 100000, "attrib-max-events");
    check(r->screenshot && r->screenshot->every == 150 && r->screenshot->from == 30, "screenshot");
    check(r->gsregs && r->gsregs->every == 600 && r->gsregs->from == 1200, "gsregs");
    check(r->gsevents && r->gsevents->every == 1 && r->gsevents->from == 2400, "gsevents");
}

void valid_limits_exactly()
{
    std::string body = repeat_lines(16, "func 0x%x calls=1024 nonzero=4096", 4);
    body += repeat_lines(4, "watch 0x%x:40", 0x100); // 4 x 64 = 256 bytes
    body += repeat_lines(16, "attrib 0x%x:100", 0x1000); // 16 x 256 = 4096 bytes
    body += "attrib-max-events 1000000\n";
    const auto r = parse_probe_spec(spec(body));
    if (!check(r.has_value(), "spec at every limit parses")) {
        std::fprintf(stderr, "  error: %s\n", k2::diag::to_string(r.error()).c_str());
    }
}

void valid_adjacent_ranges_and_top_of_ram()
{
    const auto r = parse_probe_spec(spec("watch 0x1000:4\nwatch 0x1004:4\nattrib 0x1ffffff:1\n"));
    check(r.has_value(), "adjacent ranges and last RAM byte are fine");
}

struct BadCase {
    std::string_view name;
    std::string text;
    std::size_t line; // expected error line (0 = whole file)
    std::string_view needle;
};

void malformed_specs()
{
    const BadCase cases[] = {
        {"empty file", "", 0, "missing 'k2probe 1'"},
        {"missing version", "func 0x1000\n", 1, "first directive"},
        {"wrong version", "k2probe 2\nfunc 0x1000\n", 1, "version"},
        {"version twice", spec("k2probe 1\nfunc 0x1000\n"), 2, "twice"},
        {"no probes", spec("# nothing\n"), 0, "no probes"},
        {"unknown directive", spec("trace 0x1000\n"), 2, "unknown directive"},
        {"bad hex", spec("func 0x10g0\n"), 2, "not hex"},
        {"hex too long", spec("func 0x100000000\n"), 2, "1-8 hex digits"},
        {"empty hex", spec("func 0x\n"), 2, "1-8 hex digits"},
        {"decimal-looking address is hex", spec("func 0x1000 calls=0x10\n"), 2, "decimal"},
        {"out of RAM", spec("func 0x2000000\n"), 2, "outside EE RAM"},
        {"KSEG address", spec("func 0x80100000\n"), 2, "outside EE RAM"},
        {"misaligned func", spec("func 0x1002\n"), 2, "aligned"},
        {"misaligned word", spec("func 0x1000 word=0x1001\n"), 2, "aligned"},
        {"calls zero", spec("func 0x1000 calls=0\n"), 2, "calls must be in"},
        {"calls over", spec("func 0x1000 calls=1025\n"), 2, "calls must be in"},
        {"nonzero over", spec("func 0x1000 nonzero=4097\n"), 2, "nonzero must be in"},
        {"negative count", spec("func 0x1000 calls=-1\n"), 2, "decimal"},
        {"unknown option", spec("func 0x1000 depth=3\n"), 2, "unknown option"},
        {"option twice", spec("func 0x1000 calls=1 calls=2\n"), 2, "twice"},
        {"option without value form", spec("func 0x1000 calls\n"), 2, "key=value"},
        {"duplicate func", spec("func 0x1000\nfunc 0x1000\n"), 3, "twice"},
        {"too many funcs", spec(repeat_lines(17, "func 0x%x", 4)), 18, "too many func"},
        {"arm not a func", spec("func 0x1000\narm 0x2000\n"), 3, "must also be a func"},
        {"arm twice", spec("func 0x1000\narm 0x1000\narm 0x1000\n"), 4, "only one arm"},
        {"arm after zero", spec("func 0x1000\narm 0x1000 after=0\n"), 3, "after must be in"},
        {"watch without length", spec("watch 0x1000\n"), 2, "ADDR:LEN"},
        {"watch length not word", spec("watch 0x1000:6\n"), 2, "multiple of 4"},
        {"watch range too long", spec("watch 0x1000:44\n"), 2, "length must be 1..64"},
        {"watch zero length", spec("watch 0x1000:0\n"), 2, "length must be"},
        {"watch misaligned", spec("watch 0x1002:4\n"), 2, "aligned"},
        {"watch past RAM end", spec("watch 0x1fffffc:8\n"), 2, "outside EE RAM"},
        {"watch every zero", spec("watch 0x1000:4 every=0\n"), 2, "every must be in"},
        {"too many watches", spec(repeat_lines(17, "watch 0x%x:4", 4)), 18, "too many watch"},
        {"watch bytes over", spec(repeat_lines(5, "watch 0x%x:40", 0x100)), 6, "256 bytes"},
        {"watch overlap", spec("watch 0x1000:8\nwatch 0x1004:4\n"), 0, "overlap"},
        {"attrib overlap", spec("attrib 0x1000:10\nattrib 0x100f:1\n"), 0, "overlap"},
        {"attrib bytes over", spec(repeat_lines(17, "attrib 0x%x:100", 0x1000)), 18, "too many attrib"},
        {"attrib total over", spec("attrib 0x1000:1000\nattrib 0x3000:1\n"), 3, "4096 bytes"},
        {"attrib too long", spec("attrib 0x1000:1001\n"), 2, "length must be 1..4096"},
        {"attrib trailing junk", spec("attrib 0x1000:4 0x2000:4\n"), 2, "exactly one"},
        {"attrib-max-events over", spec("attrib 0x1000:4\nattrib-max-events 1000001\n"), 3, "must be in"},
        {"attrib-max-events twice", spec("attrib 0x1000:4\nattrib-max-events 1\nattrib-max-events 2\n"), 4, "twice"},
        {"screenshot without every", spec("screenshot from=5\n"), 2, "every=N"},
        {"screenshot twice", spec("screenshot every=1\nscreenshot every=2\n"), 3, "only one"},
        {"gsregs without every", spec("gsregs from=5\n"), 2, "gsregs needs every=N"},
        {"gsregs every zero", spec("gsregs every=0\n"), 2, "every must be in"},
        {"gsregs twice", spec("gsregs every=1\ngsregs every=2\n"), 3, "only one gsregs"},
        {"gsevents without every", spec("gsevents from=5\n"), 2, "gsevents needs every=N"},
        {"gsevents twice", spec("gsevents every=1\ngsevents every=2\n"), 3, "only one gsevents"},
        {"control character", spec("func 0x1000\x01\n"), 2, "control character"},
        {"non-ASCII", spec("func 0x1000 \xC3\xA9\n"), 2, "non-ASCII"},
        {"too large", spec(std::string(Limits::kMaxFileBytes, '#')), 0, "64 KiB"},
    };
    for (const auto &c : cases) {
        const auto r = parse_probe_spec(c.text);
        std::string label(c.name);
        if (!check(!r.has_value(), label + ": rejected")) {
            continue;
        }
        check(r.error().line == c.line,
              label + ": error line " + std::to_string(r.error().line) + " (" + r.error().message + ")");
        check(r.error().message.find(c.needle) != std::string::npos,
              label + ": message '" + r.error().message + "' mentions '" + std::string(c.needle) + "'");
    }
}

void load_file_fails_closed()
{
    const auto dir = std::filesystem::temp_directory_path() / "k2diag_test_probe_spec";
    std::filesystem::create_directories(dir);
    check(!k2::diag::load_probe_file(dir / "missing.probe").has_value(), "missing file is an error");

    const auto good = dir / "good.probe";
    {
        std::ofstream(good, std::ios::binary) << "k2probe 1\nwatch 0x1000:4\n";
    }
    check(k2::diag::load_probe_file(good).has_value(), "good file loads");

    const auto big = dir / "big.probe";
    {
        std::ofstream(big, std::ios::binary) << std::string(Limits::kMaxFileBytes + 1, '#');
    }
    const auto r = k2::diag::load_probe_file(big);
    check(!r.has_value() && r.error().message.find("64 KiB") != std::string::npos, "oversize file rejected");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

} // namespace

int main()
{
    return k2diagtest::run({
        {"valid full spec", valid_full_spec},
        {"valid at every limit", valid_limits_exactly},
        {"valid adjacent ranges / top of RAM", valid_adjacent_ranges_and_top_of_ram},
        {"malformed specs are rejected with the right line", malformed_specs},
        {"load_probe_file fails closed", load_file_fails_closed},
    });
}

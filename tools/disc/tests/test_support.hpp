// SPDX-License-Identifier: GPL-3.0-or-later
// Minimal ctest helper: each test is an executable; failures print and set exit code.
// All fixtures are synthesised in memory by the builders below — never commit real data.
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <crtdbg.h>
#include <cstdlib>
#endif

namespace k2test {

inline int& failures() {
    static int n = 0;
    return n;
}

inline bool check(bool ok, std::string_view what, std::source_location loc = std::source_location::current()) {
    if (!ok) {
        std::fprintf(stderr, "FAIL %s:%u: %.*s\n", loc.file_name(), static_cast<unsigned>(loc.line()),
                     static_cast<int>(what.size()), what.data());
        ++failures();
    }
    return ok;
}

template <class A, class B>
bool check_eq(const A& a, const B& b, std::string_view what,
              std::source_location loc = std::source_location::current()) {
    return check(a == b, what, loc);
}

struct Case {
    std::string_view name;
    std::function<void()> fn;
};

// Route MSVC debug assertions/aborts to stderr instead of a modal dialog (CI must never hang).
inline void no_crt_dialogs() {
#if defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    for (const int kind : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(kind, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(kind, _CRTDBG_FILE_STDERR);
    }
#endif
}

inline int run(std::initializer_list<Case> cases) {
    no_crt_dialogs();
    for (const auto& c : cases) {
        const int before = failures();
        c.fn();
        std::fprintf(stderr, "%s %.*s\n", failures() == before ? "ok  " : "FAIL",
                     static_cast<int>(c.name.size()), c.name.data());
    }
    return failures() == 0 ? 0 : 1;
}

// ---- byte builders ----------------------------------------------------------

using Buf = std::vector<std::uint8_t>;

inline void ensure(Buf& b, std::size_t end) {
    if (b.size() < end) {
        b.resize(end, 0);
    }
}
inline void put8(Buf& b, std::size_t off, std::uint8_t v) {
    ensure(b, off + 1);
    b[off] = v;
}
inline void put16(Buf& b, std::size_t off, std::uint16_t v) {
    ensure(b, off + 2);
    b[off] = static_cast<std::uint8_t>(v);
    b[off + 1] = static_cast<std::uint8_t>(v >> 8);
}
inline void put32(Buf& b, std::size_t off, std::uint32_t v) {
    ensure(b, off + 4);
    for (int i = 0; i < 4; ++i) {
        b[off + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}
inline void put32be(Buf& b, std::size_t off, std::uint32_t v) {
    ensure(b, off + 4);
    for (int i = 0; i < 4; ++i) {
        b[off + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * (3 - i)));
    }
}
inline void put_str(Buf& b, std::size_t off, std::string_view s) {
    ensure(b, off + s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        b[off + i] = static_cast<std::uint8_t>(s[i]);
    }
}
inline Buf bytes_of(std::string_view s) {
    return Buf(s.begin(), s.end());
}

} // namespace k2test

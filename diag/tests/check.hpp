// SPDX-License-Identifier: GPL-3.0-or-later
// Minimal ctest helper for k2_diag tests (same shape as tools/disc/tests/test_support.hpp).
// All inputs are synthetic; never real game data.
#pragma once

#include <cstdio>
#include <functional>
#include <initializer_list>
#include <source_location>
#include <string_view>

#if defined(_MSC_VER)
#include <crtdbg.h>
#include <cstdlib>
#endif

namespace k2diagtest {

inline int &failures()
{
    static int n = 0;
    return n;
}

inline bool check(bool ok, std::string_view what, std::source_location loc = std::source_location::current())
{
    if (!ok) {
        std::fprintf(stderr, "FAIL %s:%u: %.*s\n", loc.file_name(), static_cast<unsigned>(loc.line()),
                     static_cast<int>(what.size()), what.data());
        ++failures();
    }
    return ok;
}

struct Case {
    std::string_view name;
    std::function<void()> fn;
};

inline int run(std::initializer_list<Case> cases)
{
#if defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    for ([[maybe_unused]] const int kind : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) { // no-ops in Release
        _CrtSetReportMode(kind, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(kind, _CRTDBG_FILE_STDERR);
    }
#endif
    for (const auto &c : cases) {
        const int before = failures();
        c.fn();
        std::fprintf(stderr, "%s %.*s\n", failures() == before ? "ok  " : "FAIL",
                     static_cast<int>(c.name.size()), c.name.data());
    }
    return failures() == 0 ? 0 : 1;
}

} // namespace k2diagtest

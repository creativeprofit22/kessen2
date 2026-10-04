# SPDX-License-Identifier: GPL-3.0-or-later
# diag: declarative probe diagnostics (ADR-0006). Game-agnostic: no Kessen II addresses here;
# game-specific probe files live in probes/.

# C++23 for std::expected (same rule as ADR-0005).
function(k2_diag_target_defaults target)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(MSVC)
        # C4324: R5900Context in ps2_runtime.h is padded by its alignas (engine header, benign).
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /wd4324)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Pure part: spec parser, write attributor, log writer. No PS2Recomp headers; always built and
# unit-tested (also in CI, where there is no generated code).
add_library(k2_diag STATIC
    src/probe_spec.cpp
    src/write_attribution.cpp
    src/probe_log.cpp
)
target_include_directories(k2_diag PUBLIC include)
k2_diag_target_defaults(k2_diag)

# Runtime binding: only with recompiled code, like k2_runtime_ext's fixes (linking ps2_runtime
# needs the function table that only generated code defines).
if(TARGET ps2_runtime AND TARGET k2_generated)
    target_sources(k2_diag PRIVATE src/probe_session.cpp src/func_probes.cpp)
    target_link_libraries(k2_diag PUBLIC ps2_runtime)
    target_compile_definitions(k2_diag PUBLIC K2_DIAG_HAS_RUNTIME=1)
else()
    target_compile_definitions(k2_diag PUBLIC K2_DIAG_HAS_RUNTIME=0)
endif()

k2_add_module(k2_diag ALLOWS ps2_runtime)

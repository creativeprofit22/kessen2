# SPDX-License-Identifier: GPL-3.0-or-later
# tools/disc: disc-image tooling (k2disc). See docs/modules/disc.md and ADR-0005.

# Disc targets use C++23 for std::expected (ADR-0005); the rest of the repo stays on C++20.
function(k2_disc_target_defaults target)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(k2_disc_core STATIC
    src/source.cpp
    src/crc32.cpp
    src/paths.cpp
    src/iso9660.cpp
    src/system_cnf.cpp
    src/disc.cpp
    src/irx.cpp
    src/tree.cpp
    src/catalogue.cpp
    src/archive_linkdata.cpp
)
target_include_directories(k2_disc_core PUBLIC src)
k2_disc_target_defaults(k2_disc_core)
k2_add_module(k2_disc_core)

add_executable(k2disc src/main.cpp)
target_link_libraries(k2disc PRIVATE k2_disc_core)
if(WIN32)
    # CommandLineToArgvW: read argv as UTF-16 instead of the ANSI code page.
    target_link_libraries(k2disc PRIVATE shell32)
endif()
k2_disc_target_defaults(k2disc)
k2_add_module(k2disc ALLOWS k2_disc_core)
target_compile_definitions(k2disc PRIVATE "K2DISC_DEFAULT_OUT=\"${CMAKE_SOURCE_DIR}/work/disc\"")

# SPDX-License-Identifier: GPL-3.0-or-later

# Module-boundary enforcement must reject forbidden edges.
add_test(NAME k2_dep_rule_negative
    COMMAND "${CMAKE_COMMAND}"
        "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/dep_violation"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/dep_violation"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_dep_violation.cmake")

# k2disc unit tests (synthetic in-memory fixtures only).
add_subdirectory("${CMAKE_SOURCE_DIR}/tools/disc/tests" disc)

# Headless boot smoke test: local only, needs recompiled code and the user's own game files
# (neither exists in CI). Run alone with: ctest --preset msvc-x64 -L boot
set(K2_BOOT_ELF "${CMAKE_SOURCE_DIR}/work/disc/SLUS-20275/iso/SLUS_202.75" CACHE FILEPATH
    "Boot ELF used by the k2_boot_smoke test")
# The main overlay load is first printed between frames 500 and 525 (Debug build); 900 leaves
# margin. The driver script also requires the boot milestone lines, see run_boot_smoke.cmake.
set(K2_BOOT_SMOKE_FRAMES 900 CACHE STRING "Frames k2_boot_smoke must present")
if(TARGET k2_generated AND EXISTS "${K2_BOOT_ELF}")
    add_test(NAME k2_boot_smoke
        COMMAND "${CMAKE_COMMAND}"
            "-DKESSEN2_EXE=$<TARGET_FILE:kessen2>"
            "-DBOOT_ELF=${K2_BOOT_ELF}"
            "-DFRAMES=${K2_BOOT_SMOKE_FRAMES}"
            "-DTIMEOUT_S=120"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_boot_smoke.cmake")
    set_tests_properties(k2_boot_smoke PROPERTIES
        LABELS boot
        TIMEOUT 180)
    message(STATUS "k2_boot_smoke: registered (${K2_BOOT_ELF})")
else()
    message(STATUS "k2_boot_smoke: skipped (needs generated code and ${K2_BOOT_ELF})")
endif()

# Git guardrail self-test (needs a POSIX sh; on Windows use Git for Windows' sh, not WSL).
find_package(Git QUIET)
set(_k2_sh_hints "")
if(GIT_EXECUTABLE)
    get_filename_component(_git_dir "${GIT_EXECUTABLE}" DIRECTORY)
    list(APPEND _k2_sh_hints "${_git_dir}/../bin" "${_git_dir}/../usr/bin")
endif()
find_program(K2_POSIX_SH NAMES sh HINTS ${_k2_sh_hints})
if(K2_POSIX_SH AND GIT_EXECUTABLE)
    add_test(NAME k2_guard
        COMMAND "${K2_POSIX_SH}" "${CMAKE_SOURCE_DIR}/tools/guard/test_guard.sh")
else()
    message(WARNING "POSIX sh or git not found: k2_guard test not registered")
endif()

# SPDX-License-Identifier: GPL-3.0-or-later

# Module-boundary enforcement must reject forbidden edges.
add_test(NAME k2_dep_rule_negative
    COMMAND "${CMAKE_COMMAND}"
        "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/dep_violation"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/dep_violation"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_dep_violation.cmake")

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

# SPDX-License-Identifier: GPL-3.0-or-later
# Driver for the k2_probe_smoke test (cmake -P): local only, like k2_boot_smoke.
# Requires: KESSEN2_EXE, BOOT_ELF, PROBE, BAD_PROBE, WORK_DIR, FRAMES, TIMEOUT_S.
# 1. A malformed spec must exit 5 (kBootProbeInvalid) before the game runs.
# 2. probes/smoke.probe must run cleanly and log every probe kind, with a probe log whose
#    lines are all well-formed and whose seq numbers increase by one.

foreach(v KESSEN2_EXE BOOT_ELF PROBE BAD_PROBE WORK_DIR FRAMES TIMEOUT_S)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} not set")
    endif()
endforeach()
unset(ENV{PS2X_IOP_TRACE})
# Removed diagnostics variables make kessen2 exit 5; keep a developer's shell out of the test.
foreach(v K2_TRACE_FUNCS K2_TRACE_WORD K2_TRACE_ARM K2_TRACE_NONZERO K2_TRACE_CD
          K2_WATCH K2_WATCH_EVERY K2_SCREENSHOT_EVERY)
    unset(ENV{${v}})
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# 1. Fail closed.
set(ENV{K2_PROBE} "${BAD_PROBE}")
unset(ENV{K2_PROBE_LOG})
execute_process(
    COMMAND "${KESSEN2_EXE}" --headless --frames 10 --timeout-s 60 --elf "${BOOT_ELF}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 5)
    message(FATAL_ERROR "malformed probe: expected exit 5, got '${rc}':\n${out}${err}")
endif()
if(NOT err MATCHES "probe: .*line [0-9]+: ")
    message(FATAL_ERROR "malformed probe: error does not name the line:\n${err}")
endif()
message(STATUS "malformed probe rejected with exit 5")

# 2. Every probe kind.
set(log_file "${WORK_DIR}/smoke.log")
set(ENV{K2_PROBE} "${PROBE}")
set(ENV{K2_PROBE_LOG} "${log_file}")
execute_process(
    COMMAND "${KESSEN2_EXE}" --headless --frames "${FRAMES}" --timeout-s "${TIMEOUT_S}" --elf "${BOOT_ELF}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "probe run exited '${rc}':\n${err}")
endif()
file(STRINGS "${log_file}" lines)
set(seen "")
set(expect_seq 0)
foreach(line IN LISTS lines)
    if(NOT line MATCHES "^frame=[0-9]+ seq=([0-9]+) kind=([a-z-]+)( [^ =]+=[^ ]+)*$")
        message(FATAL_ERROR "malformed probe log line: '${line}'")
    endif()
    if(NOT CMAKE_MATCH_1 EQUAL expect_seq)
        message(FATAL_ERROR "seq ${CMAKE_MATCH_1}, expected ${expect_seq}: '${line}'")
    endif()
    math(EXPR expect_seq "${expect_seq} + 1")
    list(APPEND seen "${CMAKE_MATCH_2}")
endforeach()
foreach(kind probe func-installed attrib-range armed call ret watch screenshot attrib-stats end)
    if(NOT kind IN_LIST seen)
        message(FATAL_ERROR "probe log has no kind=${kind} line (${log_file})")
    endif()
endforeach()
if(NOT EXISTS "${WORK_DIR}/k2-frame-000500.png")
    message(FATAL_ERROR "screenshot k2-frame-000500.png was not written")
endif()
message(STATUS "k2_probe_smoke: ${expect_seq} well-formed lines, every probe kind seen")

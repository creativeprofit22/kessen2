# SPDX-License-Identifier: GPL-3.0-or-later
# Driver for the k2_boot_smoke test (cmake -P).
# Requires: KESSEN2_EXE, BOOT_ELF, FRAMES, TIMEOUT_S.
# A clean exit alone proves nothing: the frame counter keeps running while the guest is stuck
# (docs/BRINGUP.md "Furthest point reached"). So the run must also print every boot milestone
# below, and none of the hard-failure lines the runtime logs but survives.

foreach(v KESSEN2_EXE BOOT_ELF FRAMES TIMEOUT_S)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} not set")
    endif()
endforeach()

# Trace switches change the applied-fix count and flood the log; keep the run canonical.
unset(ENV{K2_TRACE_FUNCS})
unset(ENV{K2_TRACE_CD})
unset(ENV{PS2X_IOP_TRACE})
unset(ENV{PS2X_IOP_TRACE_LIBS})
unset(ENV{PS2X_IOP_TRACE_MAX})

execute_process(
    COMMAND "${KESSEN2_EXE}" --headless --frames "${FRAMES}" --timeout-s "${TIMEOUT_S}"
            --elf "${BOOT_ELF}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
set(log "${out}${err}")

# Show the tail so a failure is diagnosable from the ctest log.
string(LENGTH "${log}" log_len)
if(log_len GREATER 4000)
    math(EXPR tail_start "${log_len} - 4000")
    string(SUBSTRING "${log}" ${tail_start} -1 log_tail)
else()
    set(log_tail "${log}")
endif()

if(NOT rc EQUAL 0)
    message(FATAL_ERROR "kessen2 exited with '${rc}'; log tail:\n${log_tail}")
endif()

# Hard failures that the runtime logs but survives (it keeps going or only stops the game
# thread), so the exit code alone would not catch them.
set(fail_patterns
    "Unimplemented PS2 (stub|syscall)"
    "Unknown syscallId"
    "Error during program execution"
    "fatal exception"
    "\\[terminate\\]"
    "guest-branch:missing-target"
    "overlay: MISMATCH")
foreach(p IN LISTS fail_patterns)
    if(log MATCHES "${p}")
        message(FATAL_ERROR "failure line matched '${p}' (\"${CMAKE_MATCH_0}\"); log tail:\n${log_tail}")
    endif()
endforeach()

# Boot milestones, in the order they are printed. Runtime output is not always newline
# terminated, so match anywhere rather than at line starts.
set(milestones
    # runtime-ext/src/kessen2_overrides.cpp: the ELF matched the Kessen II override.
    "\\[kessen2\\] game override matched"
    # Same file: every BRINGUP.md fix bound (count from fixes/apply_all.cpp, trace unset).
    "\\[kessen2\\] applied 4 fix\\(es\\)"
    # fixes/cd_overlay_guard.cpp: the game got past the disc-ready loop, read the main
    # overlay, and its header matches the recompiled code.
    "\\[kessen2\\] overlay: main overlay loaded at 0x005a4800 \\(recompiled\\)")
set(missing "")
foreach(m IN LISTS milestones)
    if(NOT log MATCHES "${m}")
        list(APPEND missing "${m}")
    endif()
endforeach()
if(missing)
    list(JOIN missing "\n  " missing_text)
    message(FATAL_ERROR "boot milestone(s) missing after ${FRAMES} frames:\n  ${missing_text}\n"
                        "log tail:\n${log_tail}")
endif()
message(STATUS "k2_boot_smoke: all milestones reached in ${FRAMES} frames")

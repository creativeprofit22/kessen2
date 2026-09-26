# SPDX-License-Identifier: GPL-3.0-or-later
# Driver for the k2_dep_rule_negative test (cmake -P).
# Requires: FIXTURE_DIR, WORK_DIR, GENERATOR (optional).
# Passes only if the valid graph configures AND every violation mode fails with the
# K2 violation message (so an unrelated configure error cannot make the test pass).

foreach(v FIXTURE_DIR WORK_DIR)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "${v} not set")
    endif()
endforeach()

function(run_mode mode out_result out_output)
    set(bin "${WORK_DIR}/${mode}")
    file(REMOVE_RECURSE "${bin}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${FIXTURE_DIR}" -B "${bin}" "-DK2_FIXTURE_MODE=${mode}"
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    set(${out_result} "${rc}" PARENT_SCOPE)
    set(${out_output} "${out}${err}" PARENT_SCOPE)
endfunction()

run_mode(none rc out)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "valid fixture graph failed to configure:\n${out}")
endif()
message(STATUS "none: configured OK")

foreach(mode IN ITEMS link indirect include unguarded)
    run_mode(${mode} rc out)
    if(rc EQUAL 0)
        message(FATAL_ERROR "mode '${mode}': forbidden dependency was NOT rejected")
    endif()
    if(NOT out MATCHES "K2 module dependency violation")
        message(FATAL_ERROR "mode '${mode}': configure failed for the wrong reason:\n${out}")
    endif()
    message(STATUS "${mode}: rejected as expected")
endforeach()

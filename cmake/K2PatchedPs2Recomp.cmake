# SPDX-License-Identifier: GPL-3.0-or-later
#
# Adds the PS2Recomp submodule to the build, with the patches listed in
# patches/ps2recomp/series applied on top of the pinned commit.
#
# The submodule itself is never modified: the pinned commit is exported with
# `git archive` into the build directory and the patches are applied there.
# An empty series uses the submodule directly. A patch that does not apply is a
# configure error (see patches/ps2recomp/README.md).

function(_k2_ps2recomp_git out_var)
    execute_process(COMMAND "${GIT_EXECUTABLE}" ${ARGN}
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _rc EQUAL 0)
        string(REPLACE ";" " " _cmd "${ARGN}")
        message(FATAL_ERROR "k2 PS2Recomp: `git ${_cmd}` failed (${_rc}): ${_err}")
    endif()
    set(${out_var} "${_out}" PARENT_SCOPE)
endfunction()

# Reads the series file: one patch filename per line, `#` starts a comment.
function(_k2_ps2recomp_read_series series_file out_var)
    file(STRINGS "${series_file}" _lines)
    set(_patches "")
    foreach(_line IN LISTS _lines)
        string(REGEX REPLACE "#.*$" "" _line "${_line}")
        string(STRIP "${_line}" _line)
        if(_line STREQUAL "")
            continue()
        endif()
        if(NOT _line MATCHES "^[A-Za-z0-9._-]+\\.patch$")
            message(FATAL_ERROR "k2 PS2Recomp: invalid entry in ${series_file}: '${_line}' "
                "(expected a bare NNNN-name.patch filename)")
        endif()
        list(APPEND _patches "${_line}")
    endforeach()
    set(${out_var} "${_patches}" PARENT_SCOPE)
endfunction()

# k2_add_patched_ps2recomp(<submodule dir> <series file>)
function(k2_add_patched_ps2recomp submodule_dir series_file)
    find_package(Git REQUIRED)
    get_filename_component(_patch_dir "${series_file}" DIRECTORY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${series_file}")

    # 1. The submodule must sit exactly at the commit the superproject pins, unmodified.
    _k2_ps2recomp_git(_head -C "${submodule_dir}" rev-parse HEAD)
    file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${submodule_dir}")
    _k2_ps2recomp_git(_tree -C "${CMAKE_SOURCE_DIR}" ls-tree HEAD "${_rel}")
    if(NOT _tree MATCHES "^160000 commit ([0-9a-f]+)")
        message(FATAL_ERROR "k2 PS2Recomp: ${_rel} is not a submodule in HEAD")
    endif()
    set(_pinned "${CMAKE_MATCH_1}")
    if(NOT _head STREQUAL _pinned)
        message(FATAL_ERROR "k2 PS2Recomp: submodule is at ${_head}, superproject pins ${_pinned}. "
            "Run: git submodule update --init --recursive")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${submodule_dir}" diff --quiet HEAD
        RESULT_VARIABLE _dirty)
    if(NOT _dirty EQUAL 0)
        message(FATAL_ERROR "k2 PS2Recomp: ${_rel} has local changes. Never edit the submodule "
            "in place; put generic fixes in ${_patch_dir} (see its README.md).")
    endif()

    # 2. Empty series: build the submodule as-is.
    _k2_ps2recomp_read_series("${series_file}" _patches)
    if(NOT _patches)
        message(STATUS "k2 PS2Recomp: ${_pinned} (no patches)")
        add_subdirectory("${submodule_dir}" external/PS2Recomp EXCLUDE_FROM_ALL)
        return()
    endif()

    # 3-5. Export + patch into the build tree when the stamp changes.
    set(_stamp_text "${_pinned}\nseries ")
    file(SHA256 "${series_file}" _h)
    string(APPEND _stamp_text "${_h}\n")
    foreach(_p IN LISTS _patches)
        set(_pf "${_patch_dir}/${_p}")
        if(NOT EXISTS "${_pf}")
            message(FATAL_ERROR "k2 PS2Recomp: ${series_file} lists missing patch ${_p}")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_pf}")
        file(SHA256 "${_pf}" _h)
        string(APPEND _stamp_text "${_p} ${_h}\n")
    endforeach()

    set(_root "${CMAKE_BINARY_DIR}/_deps/ps2recomp-patched")
    set(_src "${_root}/src")
    set(_stage "${_root}/stage")
    set(_stamp "${_root}/stamp.txt")
    set(_old_stamp "")
    if(EXISTS "${_stamp}")
        file(READ "${_stamp}" _old_stamp)
    endif()

    if(NOT _old_stamp STREQUAL _stamp_text OR NOT EXISTS "${_src}/CMakeLists.txt")
        message(STATUS "k2 PS2Recomp: exporting ${_pinned} and applying ${_patches}")
        file(REMOVE "${_stamp}")
        file(REMOVE_RECURSE "${_stage}")
        file(MAKE_DIRECTORY "${_stage}")
        # core.autocrlf=false: patches are made against LF sources.
        _k2_ps2recomp_git(_ignored -c core.autocrlf=false -C "${submodule_dir}"
            archive --format=tar "--output=${_root}/src.tar" "${_pinned}")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${_root}/src.tar"
            WORKING_DIRECTORY "${_stage}" RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "k2 PS2Recomp: extracting ${_root}/src.tar failed")
        endif()
        file(REMOVE "${_root}/src.tar")

        # The export lives inside this repo's build dir; stop git from discovering the
        # superproject so `git apply` treats paths relative to the export root.
        set(_git_env "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${_root}")
        foreach(_p IN LISTS _patches)
            set(_pf "${_patch_dir}/${_p}")
            execute_process(COMMAND ${_git_env} "${GIT_EXECUTABLE}" apply --check "${_pf}"
                WORKING_DIRECTORY "${_stage}" RESULT_VARIABLE _rc ERROR_VARIABLE _err)
            if(NOT _rc EQUAL 0)
                file(REMOVE_RECURSE "${_stage}")
                message(FATAL_ERROR "k2 PS2Recomp: patch ${_p} does not apply to ${_pinned}:\n${_err}"
                    "Regenerate it against the pinned commit or drop it from ${series_file}.")
            endif()
            execute_process(COMMAND ${_git_env} "${GIT_EXECUTABLE}" apply "${_pf}"
                WORKING_DIRECTORY "${_stage}" RESULT_VARIABLE _rc ERROR_VARIABLE _err)
            if(NOT _rc EQUAL 0)
                file(REMOVE_RECURSE "${_stage}")
                message(FATAL_ERROR "k2 PS2Recomp: applying ${_p} failed:\n${_err}")
            endif()
        endforeach()

        # Sync stage -> src touching only changed files, so editing one patch does not
        # rebuild all of PS2Recomp.
        file(GLOB_RECURSE _staged RELATIVE "${_stage}" LIST_DIRECTORIES false "${_stage}/*")
        file(GLOB_RECURSE _current RELATIVE "${_src}" LIST_DIRECTORIES false "${_src}/*")
        foreach(_f IN LISTS _current)
            if(NOT EXISTS "${_stage}/${_f}")
                file(REMOVE "${_src}/${_f}")
            endif()
        endforeach()
        foreach(_f IN LISTS _staged)
            get_filename_component(_d "${_src}/${_f}" DIRECTORY)
            file(MAKE_DIRECTORY "${_d}")
            file(COPY_FILE "${_stage}/${_f}" "${_src}/${_f}" ONLY_IF_DIFFERENT)
        endforeach()
        file(REMOVE_RECURSE "${_stage}")
        file(WRITE "${_stamp}" "${_stamp_text}")
    else()
        message(STATUS "k2 PS2Recomp: ${_pinned} + ${_patches} (up to date)")
    endif()

    # 6. Same binary dir as the unpatched build, so targets and paths do not change.
    add_subdirectory("${_src}" external/PS2Recomp EXCLUDE_FROM_ALL)
endfunction()

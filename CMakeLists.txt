# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.21)

project(kessen2 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KESSEN2_WITH_PS2RECOMP "Build against the PS2Recomp runtime submodule" ON)
set(KESSEN2_GENERATED_DIR "${CMAKE_SOURCE_DIR}/generated" CACHE PATH
    "Directory holding ps2_recomp output (never committed)")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(K2Modules)
set(K2_GENERATED_DIR "${KESSEN2_GENERATED_DIR}")

# Only the default generated/ is git-ignored and guarded; refuse any other in-tree location.
cmake_path(ABSOLUTE_PATH KESSEN2_GENERATED_DIR NORMALIZE OUTPUT_VARIABLE _k2_gen_abs)
string(REGEX REPLACE "(.)/+$" "\\1" _k2_gen_abs "${_k2_gen_abs}")
cmake_path(SET _k2_src_abs NORMALIZE "${CMAKE_SOURCE_DIR}")
if(WIN32) # case-insensitive filesystem
    string(TOLOWER "${_k2_gen_abs}" _k2_gen_abs)
    string(TOLOWER "${_k2_src_abs}" _k2_src_abs)
endif()
cmake_path(IS_PREFIX _k2_src_abs "${_k2_gen_abs}" NORMALIZE _k2_gen_in_tree)
if(_k2_gen_in_tree AND NOT _k2_gen_abs STREQUAL "${_k2_src_abs}/generated")
    message(FATAL_ERROR
        "KESSEN2_GENERATED_DIR must be ${CMAKE_SOURCE_DIR}/generated or outside the source tree "
        "(other in-tree paths are not git-ignored). Got: ${KESSEN2_GENERATED_DIR}")
endif()

include(FetchContent)

# --- SDL3 (pinned: release-3.4.16) ------------------------------------------
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG fa2c02bb6e21974a89ea9824bc53c9932abe5f9c # release-3.4.16
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(SDL3)
k2_register_guarded(SDL3::SDL3)

# --- PS2Recomp (git submodule, pinned in docs/DEPENDENCIES.md) --------------
if(KESSEN2_WITH_PS2RECOMP)
    set(_ps2recomp_dir "${CMAKE_SOURCE_DIR}/external/PS2Recomp")
    if(NOT EXISTS "${_ps2recomp_dir}/CMakeLists.txt")
        message(FATAL_ERROR
            "external/PS2Recomp is missing. Run: git submodule update --init --recursive "
            "(or configure with -DKESSEN2_WITH_PS2RECOMP=OFF).")
    endif()
    # ps2_recomp (the recompiler tool) can't be a subproject at this pin: its CMakeLists
    # includes ${CMAKE_SOURCE_DIR}/ps2xRuntime/... . Build it standalone instead
    # (see docs/DEPENDENCIES.md). Only the runtime is part of this superbuild.
    set(PS2X_BUILD_RECOMP OFF CACHE BOOL "" FORCE)
    set(PS2X_BUILD_RUNTIME ON CACHE BOOL "" FORCE)
    set(PS2X_BUILD_ANALYZER OFF CACHE BOOL "" FORCE)
    set(PS2X_BUILD_TEST OFF CACHE BOOL "" FORCE)
    set(PS2X_BUILD_STUDIO OFF CACHE BOOL "" FORCE)
    # EXCLUDE_FROM_ALL: only what our targets link gets built (not ps2EntryRunner,
    # which globs generated code from inside the submodule — see ADR-0004).
    add_subdirectory("${_ps2recomp_dir}" external/PS2Recomp EXCLUDE_FROM_ALL)
    k2_register_guarded(ps2_runtime)
endif()

# --- Recompiled game code (only when present locally) -----------------------
file(GLOB_RECURSE K2_GENERATED_SOURCES CONFIGURE_DEPENDS "${KESSEN2_GENERATED_DIR}/*.cpp")
if(K2_GENERATED_SOURCES)
    if(NOT TARGET ps2_runtime)
        message(FATAL_ERROR "Generated sources found in ${KESSEN2_GENERATED_DIR} but PS2Recomp is disabled.")
    endif()
    add_library(k2_generated STATIC ${K2_GENERATED_SOURCES})
    target_include_directories(k2_generated PUBLIC "${KESSEN2_GENERATED_DIR}")
    target_link_libraries(k2_generated PUBLIC ps2_runtime)
    k2_add_module(k2_generated ALLOWS ps2_runtime)
    # ~15k small translation units: batch them (unity build) and precompile the shared
    # runtime headers, otherwise the build is dominated by re-parsing ps2_runtime.h.
    # register_functions.cpp (one huge initializer) stays out of the batches.
    set_target_properties(k2_generated PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 64)
    set_source_files_properties("${KESSEN2_GENERATED_DIR}/register_functions.cpp"
        PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE SKIP_PRECOMPILE_HEADERS TRUE)
    target_precompile_headers(k2_generated PRIVATE
        <ps2_runtime_macros.h> <ps2_runtime.h> <ps2_syscalls.h> <ps2_stubs.h> <ps2_log.h> <stdexcept>)
    if(MSVC)
        # /MP: MSBuild hands all batches to one cl.exe; let it use every core.
        target_compile_options(k2_generated PRIVATE /bigobj /FS /MP)
    endif()
    list(LENGTH K2_GENERATED_SOURCES _k2_gen_count)
    message(STATUS "k2_generated: ${_k2_gen_count} sources in ${KESSEN2_GENERATED_DIR}")
else()
    message(STATUS "k2_generated: no sources in ${KESSEN2_GENERATED_DIR} (target not created)")
endif()

# --- Modules -----------------------------------------------------------------
add_subdirectory(tools/disc)
add_subdirectory(platform)
add_subdirectory(render)
add_subdirectory(runtime-ext)
add_subdirectory(app)

# Must run after every target_link_libraries() above.
k2_verify_module_graph()

# --- Tests -------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

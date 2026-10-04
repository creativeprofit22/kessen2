# SPDX-License-Identifier: GPL-3.0-or-later
# Fixture for k2_dep_rule_negative: a compiler-free model of the real module graph.
#   -DK2_FIXTURE_MODE=none     valid graph, must configure
#   -DK2_FIXTURE_MODE=link     k2_platform links k2_generated, must fail
#   -DK2_FIXTURE_MODE=indirect k2_platform -> helper -> k2_runtime_ext, must fail
#   -DK2_FIXTURE_MODE=include  k2_render adds the generated dir to its includes, must fail
#   -DK2_FIXTURE_MODE=unguarded k2_platform links a non-allowlisted third-party target, must fail
#   -DK2_FIXTURE_MODE=diag     k2_render links k2_diag (only runtime-ext and the app may), must fail
cmake_minimum_required(VERSION 3.21)
project(k2_dep_violation_fixture NONE)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/K2Modules.cmake")
set(K2_GENERATED_DIR "${CMAKE_CURRENT_LIST_DIR}/generated")

add_library(ps2_runtime INTERFACE)
add_library(SDL3 INTERFACE)
add_library(SDL3::SDL3 ALIAS SDL3)
k2_register_guarded(ps2_runtime SDL3::SDL3)

add_library(k2_generated INTERFACE)
target_link_libraries(k2_generated INTERFACE ps2_runtime)
add_library(k2_platform INTERFACE)
target_link_libraries(k2_platform INTERFACE SDL3::SDL3)
add_library(k2_render INTERFACE)
target_link_libraries(k2_render INTERFACE ps2_runtime k2_platform)
add_library(k2_diag INTERFACE)
target_link_libraries(k2_diag INTERFACE ps2_runtime)
add_library(k2_runtime_ext INTERFACE)
target_link_libraries(k2_runtime_ext INTERFACE ps2_runtime k2_diag)
add_library(kessen2 INTERFACE)
target_link_libraries(kessen2 INTERFACE k2_platform k2_render k2_runtime_ext k2_diag k2_generated ps2_runtime)

if(K2_FIXTURE_MODE STREQUAL "link")
    target_link_libraries(k2_platform INTERFACE k2_generated)
elseif(K2_FIXTURE_MODE STREQUAL "indirect")
    add_library(k2_fixture_helper INTERFACE)
    target_link_libraries(k2_fixture_helper INTERFACE $<BUILD_INTERFACE:k2_runtime_ext>)
    target_link_libraries(k2_platform INTERFACE k2_fixture_helper)
elseif(K2_FIXTURE_MODE STREQUAL "include")
    target_include_directories(k2_render INTERFACE "${K2_GENERATED_DIR}/sub")
elseif(K2_FIXTURE_MODE STREQUAL "unguarded")
    add_library(k2_fixture_thirdparty INTERFACE)
    target_link_libraries(k2_platform INTERFACE k2_fixture_thirdparty)
elseif(K2_FIXTURE_MODE STREQUAL "diag")
    target_link_libraries(k2_render INTERFACE k2_diag)
elseif(NOT K2_FIXTURE_MODE STREQUAL "none")
    message(FATAL_ERROR "unknown K2_FIXTURE_MODE '${K2_FIXTURE_MODE}'")
endif()

k2_add_module(k2_generated ALLOWS ps2_runtime)
k2_add_module(k2_platform ALLOWS SDL3::SDL3)
k2_add_module(k2_render ALLOWS ps2_runtime k2_platform)
k2_add_module(k2_diag ALLOWS ps2_runtime)
k2_add_module(k2_runtime_ext ALLOWS ps2_runtime k2_diag)
k2_add_module(kessen2 ALLOWS k2_platform k2_render k2_runtime_ext k2_diag k2_generated ps2_runtime SDL3::SDL3)

k2_verify_module_graph()

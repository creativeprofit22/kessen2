# SPDX-License-Identifier: GPL-3.0-or-later
add_library(k2_runtime_ext STATIC src/kessen2_overrides.cpp)
target_include_directories(k2_runtime_ext PUBLIC include)
target_compile_features(k2_runtime_ext PUBLIC cxx_std_20)

if(TARGET ps2_runtime)
    target_link_libraries(k2_runtime_ext PUBLIC ps2_runtime)
    target_compile_definitions(k2_runtime_ext PRIVATE K2_HAS_PS2_RUNTIME=1)
else()
    target_compile_definitions(k2_runtime_ext PRIVATE K2_HAS_PS2_RUNTIME=0)
endif()

k2_add_module(k2_runtime_ext ALLOWS ps2_runtime)

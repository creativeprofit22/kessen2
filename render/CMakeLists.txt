# SPDX-License-Identifier: GPL-3.0-or-later
add_library(k2_render STATIC src/render_stub.cpp)
target_include_directories(k2_render PUBLIC include)
target_link_libraries(k2_render PUBLIC k2_platform)
target_compile_features(k2_render PUBLIC cxx_std_20)

if(TARGET ps2_runtime)
    target_link_libraries(k2_render PUBLIC ps2_runtime)
    target_compile_definitions(k2_render PRIVATE K2_HAS_PS2_RUNTIME=1)
else()
    target_compile_definitions(k2_render PRIVATE K2_HAS_PS2_RUNTIME=0)
endif()

k2_add_module(k2_render ALLOWS ps2_runtime k2_platform)

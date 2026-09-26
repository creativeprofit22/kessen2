# SPDX-License-Identifier: GPL-3.0-or-later
add_library(k2_platform STATIC src/platform_sdl.cpp)
target_include_directories(k2_platform PUBLIC include)
target_link_libraries(k2_platform PRIVATE SDL3::SDL3)
target_compile_features(k2_platform PUBLIC cxx_std_20)

k2_add_module(k2_platform ALLOWS SDL3::SDL3)

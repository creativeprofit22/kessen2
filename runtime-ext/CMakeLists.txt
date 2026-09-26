# SPDX-License-Identifier: GPL-3.0-or-later
add_library(k2_runtime_ext STATIC src/kessen2_overrides.cpp)
target_include_directories(k2_runtime_ext PUBLIC include)
target_compile_features(k2_runtime_ext PUBLIC cxx_std_20)

# The Kessen II game override and its fixes exist only when recompiled code is linked:
# registering the override pulls in ps2_runtime, which needs the function table that only
# generated code defines (k2_generated is created before this directory is added).
if(TARGET ps2_runtime AND TARGET k2_generated)
    file(GLOB _k2_fix_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/fixes/*.cpp")
    target_sources(k2_runtime_ext PRIVATE ${_k2_fix_sources})
    target_include_directories(k2_runtime_ext PRIVATE src)
    target_link_libraries(k2_runtime_ext PUBLIC ps2_runtime)
    target_compile_definitions(k2_runtime_ext PRIVATE K2_HAS_GAME_OVERRIDE=1)
else()
    target_compile_definitions(k2_runtime_ext PRIVATE K2_HAS_GAME_OVERRIDE=0)
endif()

k2_add_module(k2_runtime_ext ALLOWS ps2_runtime)

# SPDX-License-Identifier: GPL-3.0-or-later
# Composition root: the only target allowed to link every module.
add_executable(kessen2 src/main.cpp)
target_compile_features(kessen2 PRIVATE cxx_std_20)
target_compile_definitions(kessen2 PRIVATE K2_VERSION="${PROJECT_VERSION}")
target_link_libraries(kessen2 PRIVATE k2_platform k2_render k2_runtime_ext)

set(_k2_app_allows k2_platform k2_render k2_runtime_ext SDL3::SDL3)
if(TARGET ps2_runtime)
    target_link_libraries(kessen2 PRIVATE ps2_runtime)
    list(APPEND _k2_app_allows ps2_runtime)
endif()
if(TARGET k2_generated)
    target_sources(kessen2 PRIVATE src/boot.cpp)
    target_link_libraries(kessen2 PRIVATE k2_generated)
    target_compile_definitions(kessen2 PRIVATE K2_HAS_GENERATED=1)
    list(APPEND _k2_app_allows k2_generated)
    # Same workaround as upstream ps2EntryRunner: WinAPI and raylib both define CloseWindow
    # (and a few other symbols); PS2Recomp links with /FORCE:MULTIPLE on MSVC.
    if(MSVC)
        target_link_options(kessen2 PRIVATE "/FORCE:MULTIPLE")
    endif()
else()
    target_compile_definitions(kessen2 PRIVATE K2_HAS_GENERATED=0)
endif()

k2_add_module(kessen2 ALLOWS ${_k2_app_allows})

# Stage runtime DLLs (SDL3, and FFmpeg when PS2Recomp is enabled) next to the executable.
if(WIN32)
    add_custom_command(TARGET kessen2 POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:kessen2>"
        VERBATIM)
    if(COMMAND ps2x_stage_ffmpeg_runtime_dlls)
        ps2x_stage_ffmpeg_runtime_dlls(kessen2)
    endif()
endif()

# SPDX-License-Identifier: GPL-3.0-or-later
# k2disc unit tests. Fixtures are built in memory by test code (never real game data).

set(K2_DISC_TESTS
    test_crc32
    test_iso9660
    test_system_cnf
    test_disc
    test_irx
    test_catalogue
    test_archive
)

foreach(t IN LISTS K2_DISC_TESTS)
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE k2_disc_core)
    target_include_directories(${t} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    k2_disc_target_defaults(${t})
    add_test(NAME k2disc_${t} COMMAND ${t})
    set_tests_properties(k2disc_${t} PROPERTIES TIMEOUT 60)
endforeach()

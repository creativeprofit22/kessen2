# SPDX-License-Identifier: GPL-3.0-or-later
# k2_diag unit tests (synthetic specs and RAM buffers only).
foreach(t IN ITEMS test_probe_spec test_write_attribution)
    add_executable(k2diag_${t} ${t}.cpp)
    target_link_libraries(k2diag_${t} PRIVATE k2_diag)
    k2_diag_target_defaults(k2diag_${t})
    add_test(NAME k2diag_${t} COMMAND k2diag_${t})
    set_tests_properties(k2diag_${t} PROPERTIES TIMEOUT 60)
endforeach()

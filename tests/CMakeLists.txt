# test_core -- Unity (ThrowTheSwitch, MIT). Compiled straight from the vendored src/, no
# library target of its own (matches upstream's own "just add the files" usage). This is
# core/'s only test framework -- keeps core/ tests exactly as portable as core/ itself, so
# they still run unchanged if Phase 7 (ARMv7 cross-build) happens. Each Unity test file
# defines its own main() (UNITY_BEGIN/RUN_TEST/UNITY_END), so each gets its own executable
# rather than being linked together -- test_core (horae) and test_ring share that shape.
add_executable(test_core
    core/test_horae.c
    ${CMAKE_SOURCE_DIR}/third_party/unity/src/unity.c
)
target_include_directories(test_core PRIVATE ${CMAKE_SOURCE_DIR}/third_party/unity/src)
target_link_libraries(test_core PRIVATE sym_core)
add_test(NAME core.horae COMMAND test_core)

add_executable(test_ring
    core/test_ring.c
    ${CMAKE_SOURCE_DIR}/third_party/unity/src/unity.c
)
target_include_directories(test_ring PRIVATE ${CMAKE_SOURCE_DIR}/third_party/unity/src)
target_link_libraries(test_ring PRIVATE sym_core)
add_test(NAME core.ring COMMAND test_ring)

# test_app -- doctest (MIT), header-only. app/'s test framework; used only for app-layer
# (C++17) logic.
add_executable(test_app
    app/test_smoke.cpp
)
target_include_directories(test_app PRIVATE ${CMAKE_SOURCE_DIR}/third_party/doctest/doctest)
target_link_libraries(test_app PRIVATE sym_core)
add_test(NAME app.smoke COMMAND test_app)

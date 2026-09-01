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

# test_corpus -- small C++ driver (see tests/corpus/test_corpus.cpp). The real Phase 1 gate
# is corpus.aggregate: >=70% aggregate message-text recall across the whole corpus. Not
# 100%/per-file, because ft8_lib's own unmodified reference demo -- built and run directly
# against this same corpus to rule out a bug in argus.c -- only achieves 73% (265/362); the
# corpus's ground truth was almost certainly produced by WSJT-X, whose decoder is more
# sophisticated. See .claude/state/context.md. Per-file corpus.<name> tests still run for
# diagnosing a *regression* (a file that used to decode and stopped) -- they are expected to
# fail individually sometimes and that is not, by itself, a gate failure; corpus.aggregate is
# the number that matters. Revisit the 70% threshold if the decode algorithm improves.
add_executable(test_corpus
    corpus/test_corpus.cpp
)
target_link_libraries(test_corpus PRIVATE sym_core ft8_lib_wave)

set(SYM_CORPUS_DIR "${CMAKE_SOURCE_DIR}/third_party/ft8_lib/test/wav")
add_test(NAME corpus.aggregate COMMAND test_corpus --aggregate "${SYM_CORPUS_DIR}" 70)

file(GLOB SYM_CORPUS_WAV_FILES CONFIGURE_DEPENDS "${SYM_CORPUS_DIR}/*.wav")
foreach(_wav_file IN LISTS SYM_CORPUS_WAV_FILES)
    get_filename_component(_wav_name "${_wav_file}" NAME_WE)
    set(_txt_file "${SYM_CORPUS_DIR}/${_wav_name}.txt")
    if(EXISTS "${_txt_file}")
        add_test(NAME "corpus.${_wav_name}" COMMAND test_corpus "${_wav_file}" "${_txt_file}")
    endif()
endforeach()

# hal/ -- the platform seam. One implementation file per target per header, selected here by
# which files get compiled, never by #ifdef in shared source (see the kickoff's final
# guardrails). core/ never links this directly; only app/ does, via sym_host_t (see
# core/sym_types.h).
add_library(sym_hal STATIC
    hal_types.h
    hal_time.h
    hal_audio.h
    hal_cat.h
)

target_include_directories(sym_hal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    target_sources(sym_hal PRIVATE time_win.c)
else()
    target_sources(sym_hal PRIVATE time_posix.c)
endif()

# audio_x6200.c / cat_x6200.c don't exist yet -- the kickoff leaves them genuinely
# unimplemented stubs until someone characterizes the on-radio backend IPC (Phase 7). Only
# the PC-side backends are wired up here.
target_sources(sym_hal PRIVATE
    audio_miniaudio.c
    cat_hamlib.c
)

target_include_directories(sym_hal PRIVATE ${CMAKE_SOURCE_DIR}/third_party/miniaudio)

if(WIN32)
    # miniaudio's WASAPI backend resolves most of the Win32 API it needs via LoadLibrary at
    # runtime, but a few symbols (CoInitializeEx/CoUninitialize, PropVariantClear) still
    # need linking against these import libraries directly.
    target_link_libraries(sym_hal PRIVATE ole32 winmm)

    # Hamlib -- vendored dev SDK, see third_party/hamlib-w64/VERSION.md. `hamlib` is the
    # imported target third_party/CMakeLists.txt defines for it.
    target_link_libraries(sym_hal PUBLIC hamlib)
else()
    # ALSA backend, PC Linux.
    target_link_libraries(sym_hal PRIVATE asound)

    # Hamlib -- system package on Linux, not vendored (see third_party/hamlib-w64/VERSION.md
    # for why Windows differs). Unverified on this machine (no Linux box); first real proof
    # is CI, same caveat as the linux-x64 presets themselves.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HAMLIB REQUIRED IMPORTED_TARGET hamlib)
    target_link_libraries(sym_hal PUBLIC PkgConfig::HAMLIB)
endif()

# hal_types.h reaches core/sym_types.h via a relative "../core/sym_types.h" include -- no
# extra include path needed for that; core/ has zero dependencies of its own to pull in.

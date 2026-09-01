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

# hal_types.h reaches core/sym_types.h via a relative "../core/sym_types.h" include -- no
# extra include path needed for that; core/ has zero dependencies of its own to pull in.

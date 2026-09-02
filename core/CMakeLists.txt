# core/ -- portable C11, zero OS dependencies (no file I/O, no sockets, no threads, no clock
# reads, no printf). Must build unchanged for a future ARMv7 (X6200) target. Links only
# ft8_lib; never sym_hal, never anything platform-specific -- see core/sym_types.h's
# sym_host_t for how the host injects clock/PTT access instead.
add_library(sym_core STATIC
    sym_types.h
    api.h
    api.c
    horae.h
    horae.c
    ring.h
    ring.c
    argus.h
    argus.c
    cerberus.h
    cerberus.c
    qso.h
    qso.c
    mnemosyne.h
    mnemosyne.c
    atropos.h
    atropos.c
    tx.h
    tx.c
)

target_include_directories(sym_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sym_core PUBLIC ft8_lib)

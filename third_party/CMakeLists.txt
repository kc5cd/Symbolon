# Everything under this directory is vendored, unmodified, from upstream (see each
# subdirectory's own VERSION.md or submodule pin). ft8_lib ships no CMake of its own (a
# plain Makefile only) -- rather than add_subdirectory()-ing anything or patching one in,
# this file mirrors its libft8.a source list itself, entirely on our side of the boundary.

# ft8_lib (kgoba, MIT), pinned in .gitmodules to a specific commit -- see that file's
# comment for why (no tagged release matches the API surface this project was designed
# against; verified directly by diffing tag 2.0 against master HEAD).
#
# fft/ (kissfft) is included here, unlike upstream's own libft8.a -- Phase 1's core/argus.c
# links fft/ directly for the STFT/waterfall build, per the kickoff's vendoring plan.
# Upstream itself only ever links fft/ into its own decode_ft8 demo, never into libft8.a.
add_library(ft8_lib STATIC
    ft8_lib/ft8/constants.c
    ft8_lib/ft8/constants.h
    ft8_lib/ft8/crc.c
    ft8_lib/ft8/crc.h
    ft8_lib/ft8/decode.c
    ft8_lib/ft8/decode.h
    ft8_lib/ft8/encode.c
    ft8_lib/ft8/encode.h
    ft8_lib/ft8/ldpc.c
    ft8_lib/ft8/ldpc.h
    ft8_lib/ft8/message.c
    ft8_lib/ft8/message.h
    ft8_lib/ft8/text.c
    ft8_lib/ft8/text.h
    ft8_lib/ft8/debug.h
    ft8_lib/fft/kiss_fft.c
    ft8_lib/fft/kiss_fft.h
    ft8_lib/fft/kiss_fftr.c
    ft8_lib/fft/kiss_fftr.h
)
target_include_directories(ft8_lib PUBLIC ft8_lib)

if(WIN32)
    # See cmake/compat/win_stpcpy.h's own comment: ft8/message.c calls stpcpy()
    # unconditionally with no local declaration, and MinGW-w64's <string.h> doesn't declare
    # it. Force-included ahead of every translation unit in this target rather than touching
    # the vendored source.
    target_compile_options(ft8_lib PRIVATE
        -include "${CMAKE_SOURCE_DIR}/cmake/compat/win_stpcpy.h"
    )
endif()

# common/wave.c (WAV I/O) -- file I/O, so only app/ and the Phase 1 test corpus link this;
# core/ never does (core/ makes no OS calls at all, per the kickoff's architecture section).
# Kept as its own tiny static library rather than folded into ft8_lib above, so that
# boundary can't blur by accident. Not linked by anything yet in Phase 0 -- first real use
# is the Phase 1 WAV regression corpus.
add_library(ft8_lib_wave STATIC
    ft8_lib/common/wave.c
    ft8_lib/common/wave.h
)
target_include_directories(ft8_lib_wave PUBLIC ft8_lib)

# Unity (ThrowTheSwitch, MIT) -- core/'s C11 test framework. Three files; compiled straight
# into tests/core rather than given its own library target here, matching upstream's own
# "just add the files" usage pattern.

# doctest (MIT) -- app/'s C++17 test framework. Header-only; tests/app only needs the
# include path, added directly in tests/CMakeLists.txt.

# Hamlib (LGPL-2.1), Windows only -- a trimmed, checksum-verified snapshot of the official
# win64 release zip (no upstream git repo suitable for a submodule pin; see
# hamlib-w64/VERSION.md for the exact version, checksum, and what was trimmed out). Linux
# never vendors this -- hal/CMakeLists.txt finds a system Hamlib via pkg-config instead, per
# the kickoff's "pkg-config on Linux, an imported target on Windows" split.
if(WIN32)
    set(HAMLIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/hamlib-w64")

    add_library(hamlib SHARED IMPORTED GLOBAL)
    set_target_properties(hamlib PROPERTIES
        IMPORTED_IMPLIB "${HAMLIB_ROOT}/lib/libhamlib.dll.a"
        IMPORTED_LOCATION "${HAMLIB_ROOT}/bin/libhamlib-4.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${HAMLIB_ROOT}/include"
    )

    # libhamlib-4.dll's own runtime dependencies (confirmed via `objdump -p`, see
    # VERSION.md) -- anything linking the `hamlib` target on Windows needs these sitting
    # next to its executable, not just the import library at link time.
    # libwinpthread-1.dll is deliberately NOT in this list even though libhamlib-4.dll
    # depends on it (see VERSION.md) -- Hamlib's own build of that DLL is a different
    # size/hash than the one this project's own executables already need from the MinGW
    # toolchain, and since Windows searches an exe's own directory before PATH, copying it
    # here would shadow the correct copy and break every exe in this directory, hamlib-using
    # or not. Confirmed by an actual STATUS_DLL_NOT_FOUND failure, not a hypothetical --
    # left off PATH resolution instead, same as this project's other MinGW runtime DLLs.
    set(HAMLIB_RUNTIME_DLLS
        "${HAMLIB_ROOT}/bin/libhamlib-4.dll"
        "${HAMLIB_ROOT}/bin/libusb-1.0.dll"
        CACHE INTERNAL "Hamlib runtime DLLs to copy next to any exe linking the hamlib target"
    )
endif()

# SQLite (public domain), amalgamation snapshot -- see sqlite/VERSION.md for the exact
# version/checksum. app/sqlite_sink.cpp (mnemosyne.c's app-side sink) is its only consumer;
# core/ never links this directly (core/ makes no OS calls, see .claude/CLAUDE.md).
add_library(sqlite3 STATIC
    sqlite/sqlite3.c
    sqlite/sqlite3.h
)
target_include_directories(sqlite3 PUBLIC sqlite)
if(NOT WIN32)
    # SQLite's amalgamation needs these on a typical Linux build (pthreads for its default
    # threading mode, libdl for the loadable-extension path, libm for the math functions its
    # query planner uses) -- Windows gets them from the CRT/kernel32 instead, no extra link
    # needed there.
    target_link_libraries(sqlite3 PUBLIC pthread dl m)
endif()

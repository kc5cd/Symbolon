#ifndef SYM_COMPAT_WIN_STPCPY_H
#define SYM_COMPAT_WIN_STPCPY_H

/* Force-included (see third_party/CMakeLists.txt's -include flag on the ft8_lib target,
   WIN32 only) rather than #include-d from any source file -- ft8_lib/ft8/message.c calls
   stpcpy() unconditionally with no local declaration of its own, relying on the host libc's
   <string.h> to declare it (true on glibc when _POSIX_C_SOURCE >= 200809L). MinGW-w64's
   <string.h> does not declare stpcpy at all (confirmed empirically: not present anywhere
   under the toolchain's include tree), which would otherwise fail to compile outright --
   an implicit declaration of a pointer-returning function is a hard error in C11, not a
   warning. This is a Windows-toolchain gap, not a bug in ft8_lib's own portable C; the
   guardrail against modifying vendored sources is honored by fixing it here instead, purely
   at the CMake/compile-option layer. */

#include <string.h>

#if defined(_WIN32) && !defined(SYM_HAVE_STPCPY_SHIM)
#define SYM_HAVE_STPCPY_SHIM 1
static inline char* stpcpy(char* dst, const char* src)
{
    size_t len = strlen(src);
    memcpy(dst, src, len + 1);
    return dst + len;
}
#endif

#endif /* SYM_COMPAT_WIN_STPCPY_H */

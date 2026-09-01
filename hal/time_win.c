#include "hal_time.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

uint64_t hal_time_utc_us(void)
{
    /* FILETIME: 100 ns intervals since 1601-01-01 UTC. GetSystemTimePreciseAsFileTime gives
       sub-millisecond accuracy, which matters for the kickoff's "+/-2 s slot tolerance"
       clock-sync check at startup. */
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    /* 116444736000000000 = 100 ns intervals between 1601-01-01 and 1970-01-01 */
    uint64_t hundred_ns_since_unix_epoch = li.QuadPart - 116444736000000000ULL;
    return hundred_ns_since_unix_epoch / 10ULL;
}

uint64_t hal_time_mono_us(void)
{
    static LARGE_INTEGER s_freq = { 0 };
    if (s_freq.QuadPart == 0) {
        QueryPerformanceFrequency(&s_freq);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    /* Split into whole-seconds and remainder before multiplying by 1,000,000 -- avoids
       overflowing a 64-bit intermediate on a long-running session (QueryPerformanceCounter's
       origin is boot time, not process start). */
    uint64_t whole_seconds = (uint64_t)(now.QuadPart / s_freq.QuadPart);
    uint64_t remainder = (uint64_t)(now.QuadPart % s_freq.QuadPart);
    return whole_seconds * 1000000ULL + (remainder * 1000000ULL) / (uint64_t)s_freq.QuadPart;
}

void hal_time_sleep_us(uint64_t us)
{
    /* Sub-millisecond sleeps are meaningless on stock Windows timer resolution; round up so
       callers asking for a short wait actually get one rather than returning immediately. */
    DWORD ms = (DWORD)((us + 999) / 1000);
    Sleep(ms);
}

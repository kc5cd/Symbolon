#include "hal_time.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

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

hal_rc_t hal_time_ntp_synced(bool* out_synced)
{
    if (out_synced == NULL) {
        return HAL_RC_INVALID_ARG;
    }

    /* No direct Win32 API reports "am I NTP-synced" the way w32tm's own status query does --
       shelling out to the same tool an operator would run by hand is the pragmatic choice
       here (see issue #7). 2>&1 folds stderr in so a stopped W32Time service still produces
       parseable/empty output rather than silently vanishing. */
    FILE* pipe = _popen("w32tm /query /status 2>&1", "r");
    if (pipe == NULL) {
        return HAL_RC_ERROR;
    }

    char line[256];
    bool found_source_line = false;
    bool synced = false;
    while (fgets(line, sizeof(line), pipe) != NULL) {
        if (strncmp(line, "Source:", 7) == 0) {
            found_source_line = true;
            /* Unsynced states report "Local CMOS Clock" (never synced since boot) or
               "Free-running System Clock" (service running but unreachable) -- anything else
               names a real time source, meaning w32tm itself considers the clock synced. */
            synced = (strstr(line, "Local CMOS Clock") == NULL)
                  && (strstr(line, "Free-running System Clock") == NULL);
        }
    }
    _pclose(pipe);

    if (!found_source_line) {
        /* w32tm not installed, service not running, output didn't parse -- unknown, not
           "unsynced"; let the caller decide how to treat that (see hal_time.h's doc comment). */
        return HAL_RC_ERROR;
    }
    *out_synced = synced;
    return HAL_RC_OK;
}

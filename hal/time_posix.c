#include "hal_time.h"

#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <stdio.h>
#include <string.h>

uint64_t hal_time_utc_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

uint64_t hal_time_mono_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

void hal_time_sleep_us(uint64_t us)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(us / 1000000ULL);
    ts.tv_nsec = (long)((us % 1000000ULL) * 1000ULL);
    while (nanosleep(&ts, &ts) == -1) {
        /* interrupted by a signal; nanosleep already updated ts to the remainder -- retry */
    }
}

/* chronyc reports a direct "Leap status" verdict; tried first since it's the more precise
   signal when present. Returns true (and sets *out_synced) only if a parseable line was
   found -- a missing/non-executable chronyc binary is exactly the "try the next thing"
   case, not an error. */
static bool try_chronyc(bool* out_synced)
{
    FILE* pipe = popen("chronyc tracking 2>/dev/null", "r");
    if (pipe == NULL) {
        return false;
    }
    char line[256];
    bool found = false;
    while (fgets(line, sizeof(line), pipe) != NULL) {
        if (strncmp(line, "Leap status", 11) == 0) {
            found = true;
            *out_synced = (strstr(line, "Normal") != NULL);
        }
    }
    pclose(pipe);
    return found;
}

/* Fallback for systems running only systemd-timesyncd (no chrony installed) --
   "System clock synchronized:" is timedatectl's own plain-English verdict. */
static bool try_timedatectl(bool* out_synced)
{
    FILE* pipe = popen("timedatectl status 2>/dev/null", "r");
    if (pipe == NULL) {
        return false;
    }
    char line[256];
    bool found = false;
    while (fgets(line, sizeof(line), pipe) != NULL) {
        if (strstr(line, "System clock synchronized:") != NULL) {
            found = true;
            *out_synced = (strstr(line, "yes") != NULL);
        }
    }
    pclose(pipe);
    return found;
}

hal_rc_t hal_time_ntp_synced(bool* out_synced)
{
    if (out_synced == NULL) {
        return HAL_RC_INVALID_ARG;
    }
    /* Written blind, like the rest of this project's POSIX code -- no Linux box exists on
       this dev machine (see .claude/CLAUDE.md's Linux-preset note); first real proof is CI
       or the eventual X6200 (ARMv7 Linux) target, which is why this tries two different
       time-sync tools rather than assuming either is installed. */
    if (try_chronyc(out_synced)) {
        return HAL_RC_OK;
    }
    if (try_timedatectl(out_synced)) {
        return HAL_RC_OK;
    }
    return HAL_RC_ERROR;
}

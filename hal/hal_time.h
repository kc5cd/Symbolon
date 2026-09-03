#ifndef HAL_TIME_H
#define HAL_TIME_H

#include <stdbool.h>
#include <stdint.h>
#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Implemented by hal/time_win.c (Windows) or hal/time_posix.c (Linux), selected in CMake --
   never by #ifdef in shared source. Both feed a sym_host_t (core/sym_types.h) at startup;
   core/ never calls these directly. */

/* Microseconds since the Unix epoch, UTC. Used for FT8 15 s slot alignment -- callers should
   expect ordinary wall-clock behavior (can step on NTP correction), which is exactly why
   horae.c's slot math and atropos.c's watchdog use two different clocks (see hal_time_mono_us). */
uint64_t hal_time_utc_us(void);

/* Monotonic, arbitrary origin, never steps backward and never jumps on NTP correction. Used
   for watchdog/timeout/duration math (atropos.c) where a stepped clock would be dangerous. */
uint64_t hal_time_mono_us(void);

void hal_time_sleep_us(uint64_t us);

/* Best-effort query of whether the OS's own time service reports itself NTP-synced -- a
   simple synced/not-synced bool, no numeric staleness threshold (see GitHub issue #7's
   design discussion: FT8's tight decode-alignment window makes trusting the OS's own sync
   verdict simpler and no less safe than picking and justifying an age cutoff ourselves).
   Returns HAL_RC_ERROR if the status genuinely couldn't be determined -- callers should
   treat that as "unknown", not "not synced", and *out_synced is left unwritten in that case. */
hal_rc_t hal_time_ntp_synced(bool* out_synced);

#ifdef __cplusplus
}
#endif

#endif /* HAL_TIME_H */

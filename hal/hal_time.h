#ifndef HAL_TIME_H
#define HAL_TIME_H

#include <stdint.h>

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

#ifdef __cplusplus
}
#endif

#endif /* HAL_TIME_H */

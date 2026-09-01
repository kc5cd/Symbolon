#ifndef HORAE_H
#define HORAE_H

#include <stdint.h>

/* Horae: in Greek myth, the goddesses who guarded the gates of Olympus and regulated the
   seasons and hours -- timing and gatekeeping in one figure, which is exactly this module's
   job (UTC 15 s slot alignment/slicing gates when core/ is allowed to look for a signal). */

#ifdef __cplusplus
extern "C" {
#endif

/* UTC 15 s slot alignment -- the Horae guard Olympus' gates: timing and gatekeeping. Pure
   function, no clock read (core/ makes no OS calls) -- callers pass in whatever
   sym_host_t.utc_us() returned. This is also the template shape for every other core/ test:
   deterministic in, deterministic out, no HAL dependency needed to exercise it. */

#define HORAE_SLOT_US 15000000ULL /* FT8_SLOT_TIME = 15.0 s, per the kickoff's protocol reference */

typedef struct {
    uint64_t slot_epoch_us; /* UTC microseconds at the start of the slot containing utc_us */
    uint64_t offset_us;     /* elapsed since slot_epoch_us; always in [0, HORAE_SLOT_US) */
} horae_slot_t;

horae_slot_t horae_slot_at(uint64_t utc_us);

#ifdef __cplusplus
}
#endif

#endif /* HORAE_H */

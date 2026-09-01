#ifndef SYM_TYPES_H
#define SYM_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYM_RC_OK = 0,
    SYM_RC_ERROR,       /* generic failure, see out-of-band error string where available */
    SYM_RC_INVALID_ARG,
    SYM_RC_UNSUPPORTED, /* valid call, but this target/backend cannot do it (e.g. cat_x6200 stub) */
    SYM_RC_TIMEOUT,
    SYM_RC_DENIED,      /* tier check failed */
} sym_rc_t;

/* Host services core needs but must never obtain for itself (core/ makes no OS calls).
   The host (app/main.cpp today; a future radio-side main.c) fills this once at startup and
   hands it to core at init. See core/api.h. */
typedef struct {
    uint64_t (*mono_us)(void* user); /* monotonic clock, arbitrary origin, never steps backward */
    uint64_t (*utc_us)(void* user);  /* wall clock, microseconds since Unix epoch, UTC */
    sym_rc_t (*ptt_set)(void* user, int assert_tx); /* atropos.c must be able to force PTT off */
    void*    user;
} sym_host_t;

#ifdef __cplusplus
}
#endif

#endif /* SYM_TYPES_H */

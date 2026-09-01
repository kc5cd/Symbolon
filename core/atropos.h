#ifndef ATROPOS_H
#define ATROPOS_H

/* Atropos: one of the three Fates in Greek myth -- the one who cuts the thread of life,
   without negotiation, when its time comes. Matches this module's job: it cuts a
   transmission short (or refuses to key at all) the instant a safety interlock trips, no
   second chances. See the kickoff's "Safety interlocks -- required before Phase 4 ships"
   section for the four interlocks this implements. */

#include <stdbool.h>
#include <stdint.h>
#include "sym_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 15s-slot minimum spacing means at most 3600/15 = 240 TX slots could ever occur within a
   real rolling hour -- sized as a ring buffer, not a growth-bounded log. */
#define ATROPOS_MAX_TX_SLOT_HISTORY 240
#define ATROPOS_MAX_ALLOWED_FREQS 16

typedef struct {
    /* Force PTT off if it's been continuously asserted this long. The kickoff specifies
       13.5 s exactly (12.64 s real TX duration + margin) -- not a tunable safety margin,
       treat it as a spec constant, not a policy choice. */
    uint64_t ptt_watchdog_us;

    /* Auto-disarm after this long with no atropos_operator_input() call. 0 = disabled
       (armed/beacon modes with no dead-man timer at all) -- deliberately not defaulted to a
       nonzero value by this module; whoever configures autonomy policy picks the number. */
    uint64_t dead_man_timeout_us;

    /* 0 = unlimited. Checked via atropos_tx_budget_ok() before every key-down, not enforced
       after the fact. */
    int max_tx_slots_per_hour;
    uint64_t max_tx_us_session; /* hard session cap; 0 = unlimited */

    /* Dial (VFO) frequencies a transmission is allowed to occur on, each with the same
       +/- freq_tolerance_hz window -- validates *where the rig is tuned*, not the audio tone
       offset within the channel. Empty (count == 0) allows nothing, matching cerberus.c's
       "empty whitelist matches no one" precedent -- fail closed, not open. */
    uint64_t allowed_freq_hz[ATROPOS_MAX_ALLOWED_FREQS];
    int allowed_freq_count;
    uint64_t freq_tolerance_hz;
} atropos_config_t;

typedef struct {
    atropos_config_t config;
    sym_host_t host; /* mono_us clock + ptt_set callback -- the same host-injection seam
                         every other core/ module is built around, but the first one that
                         actually needs it: everything up to this phase (argus.c, cerberus.c,
                         qso.c) is pure decode/predicate/state-machine logic with no clock
                         reads or PTT control at all. */

    bool ptt_asserted;
    uint64_t ptt_asserted_at_us;

    bool armed;
    uint64_t last_operator_input_us;

    uint64_t tx_slot_start_us[ATROPOS_MAX_TX_SLOT_HISTORY]; /* ring buffer */
    uint32_t tx_slot_count; /* keeps counting past ATROPOS_MAX_TX_SLOT_HISTORY; only the most
                                recent ATROPOS_MAX_TX_SLOT_HISTORY entries are ever valid --
                                see atropos.c's own indexing */
    uint64_t total_tx_us_session;
} atropos_t;

void atropos_init(atropos_t* atropos, const atropos_config_t* config, const sym_host_t* host);

/* Validates `freq_hz` (the rig's current dial frequency) against the configured allowlist.
   Call before every key-down. */
bool atropos_freq_allowed(const atropos_t* atropos, uint64_t freq_hz);

/* Checks the TX-slot-per-hour and session-TX-time budgets -- both are refusals to key, not
   post-hoc accounting. Call alongside atropos_freq_allowed() before every key-down. */
bool atropos_tx_budget_ok(const atropos_t* atropos);

/* Call exactly once, right when PTT is actually asserted for a new transmission -- starts the
   watchdog clock and records one TX slot against the budget tracking. The caller is
   responsible for having actually called sym_host_t.ptt_set(true) itself; this only tracks. */
void atropos_ptt_asserted(atropos_t* atropos);

/* Call once, right when PTT is released normally (the transmission finished on its own,
   before the watchdog needed to step in). Adds the elapsed time to the session TX-time
   total. */
void atropos_ptt_released(atropos_t* atropos);

/* Poll regularly (e.g. once per decode-loop iteration) while PTT might be asserted --
   force-releases PTT via sym_host_t.ptt_set(user, 0) if it's been continuously asserted past
   config.ptt_watchdog_us. Returns true iff it just force-released (a caller should log this
   loudly -- it means something hung and the safety net caught it). A no-op (returns false)
   when PTT isn't currently asserted. */
bool atropos_watchdog_tick(atropos_t* atropos);

/* Call whenever the operator provides input (a confirm keypress, an --arm command, etc.) --
   resets the dead-man timer. */
void atropos_operator_input(atropos_t* atropos);

/* Poll regularly while armed -- auto-disarms if config.dead_man_timeout_us has elapsed since
   the last atropos_operator_input()/atropos_arm() call. Returns true iff it just
   auto-disarmed. A no-op (returns false) when not armed or when the timer is disabled
   (dead_man_timeout_us == 0). */
bool atropos_dead_man_tick(atropos_t* atropos);

void atropos_arm(atropos_t* atropos);   /* also resets the dead-man timer */
void atropos_disarm(atropos_t* atropos);

#ifdef __cplusplus
}
#endif

#endif /* ATROPOS_H */

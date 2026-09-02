#ifndef MNEMOSYNE_H
#define MNEMOSYNE_H

/* Mnemosyne: Titaness of Memory -- this module's only job is remembering what was heard,
   independent of whether it ever became a worked QSO. See cerberus.h's own note: "heard but
   not worked" is propagation evidence per the kickoff and must still be recorded by the
   caller (mnemosyne.c, this module) -- cerberus.c itself only ever reports a match/no-match
   verdict, it never keeps a record. */

#include <stdint.h>
#include <stdbool.h>
#include "argus.h"
#include "cerberus.h"
#include "qso.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MNEMOSYNE_CALLSIGN_MAX 12
#define MNEMOSYNE_TEXT_MAX 32
#define MNEMOSYNE_BAND_MAX 8
#define MNEMOSYNE_DEDUP_CAPACITY 32 /* generous vs. a single slot's realistic candidate count */

typedef struct {
    uint64_t utc_us;
    char call_de[MNEMOSYNE_CALLSIGN_MAX]; /* "" when the decode had no structured call_de --
                                              e.g. a beacon-token free-text match, see argus.h */
    char call_to[MNEMOSYNE_CALLSIGN_MAX];
    char band[MNEMOSYNE_BAND_MAX]; /* caller-supplied current band, same convention as
                                       cerberus_config_t.band -- "" if not tracked */
    char text[MNEMOSYNE_TEXT_MAX];
    float freq_hz;
    float snr_db;
    bool is_beacon_token;
} mnemosyne_observation_t;

typedef struct {
    uint64_t utc_us;
    char my_call[MNEMOSYNE_CALLSIGN_MAX];
    char peer_call[MNEMOSYNE_CALLSIGN_MAX];
    int snr_i_sent;
    int snr_i_got;
    int asymmetry_db; /* snr_i_got - snr_i_sent -- the kickoff's own headline measurement */
} mnemosyne_qso_t;

/* core/ makes no OS calls (see .claude/CLAUDE.md) -- every observation/QSO record is handed
   to the host via these callbacks instead of ever being written to a file directly.
   app/sqlite_sink.cpp is today's only implementation. Either pointer may be NULL to ignore
   that record type. */
typedef struct {
    void (*on_observation)(void* user, const mnemosyne_observation_t* obs);
    void (*on_qso_complete)(void* user, const mnemosyne_qso_t* qso);
    void* user;
} mnemosyne_sink_t;

typedef struct {
    mnemosyne_sink_t sink;

    /* Per-slot dedup: argus_decode_slot() does not deduplicate (the same message can be
       found via multiple candidates -- see argus.h's own note), so mnemosyne keeps its own
       small "already logged this slot" set, keyed on (call_de, text), cleared each slot by
       mnemosyne_slot_reset(). A fixed array, not qso.c's per-slot state -- mnemosyne tracks
       every heard whitelisted station, not just the one currently mid-exchange. */
    char dedup_call[MNEMOSYNE_DEDUP_CAPACITY][MNEMOSYNE_CALLSIGN_MAX];
    char dedup_text[MNEMOSYNE_DEDUP_CAPACITY][MNEMOSYNE_TEXT_MAX];
    int dedup_count;
} mnemosyne_t;

void mnemosyne_init(mnemosyne_t* m, const mnemosyne_sink_t* sink);

/* Call once per new slot (alongside argus_reset()) -- clears the dedup set only, sink stays. */
void mnemosyne_slot_reset(mnemosyne_t* m);

/* Logs `decode` iff result->whitelist_ok (heard a whitelisted station at all -- deliberately
   ignores directed_at_me_ok/text_ok/gates_ok, which only matter for whether to *reply*, not
   whether the propagation event is worth recording -- see the kickoff's core research goal
   of measuring bidirectional HF propagation between two specific stations) and it isn't a
   dedup within this slot. current_band/utc_us are caller-supplied, same convention as
   cerberus_evaluate()'s own current_band parameter -- pass NULL/"" if band isn't tracked.
   A no-op once the dedup set is full for this slot (MNEMOSYNE_DEDUP_CAPACITY) -- silently
   drops further decodes rather than growing without bound or risking a double-logged row. */
void mnemosyne_observe(mnemosyne_t* m, const argus_decode_t* decode, const cerberus_result_t* result,
                        const char* current_band, uint64_t utc_us);

/* Call when qso->step == QSO_STEP_COMPLETE (before qso_reset() clears it) -- builds and
   emits the final QSO record. No-op if either SNR direction is still invalid (shouldn't
   happen at QSO_STEP_COMPLETE per qso.h's own contract, but this function doesn't re-verify
   that contract beyond checking the two valid flags itself). */
void mnemosyne_log_qso(mnemosyne_t* m, const qso_t* qso, const qso_config_t* cfg, uint64_t utc_us);

#ifdef __cplusplus
}
#endif

#endif /* MNEMOSYNE_H */

#ifndef CERBERUS_H
#define CERBERUS_H

/* Cerberus: the three-headed hound guarding the underworld's gate -- matches this module's
   job of gating which decodes pass through as a real match, several predicate categories
   (whitelist, directed-at-me, text, gates) that must all agree before anything gets through. */

#include <stdbool.h>
#include "argus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CERBERUS_CALLSIGN_MAX 12
#define CERBERUS_MAX_WHITELIST 16
#define CERBERUS_BEACON_TOKEN_MAX 32
#define CERBERUS_BAND_MAX 8

typedef struct {
    /* "Directed at me" checks a decode's call_to against this. Required for the whitelist/
       directed-at-me predicates on the non-beacon path; ignored on the beacon-token path
       unless beacon_requires_callsign_match is set (see below). */
    char my_call[CERBERUS_CALLSIGN_MAX];

    /* call_de must equal one of these (exact, case-sensitive -- FT8 callsigns are already
       normalized to uppercase by ft8_lib's own encode/decode) to satisfy the whitelist
       predicate. An empty whitelist (count==0) matches no one -- explicit opt-in, not a
       wildcard-by-default gate. */
    char whitelist[CERBERUS_MAX_WHITELIST][CERBERUS_CALLSIGN_MAX];
    int whitelist_count;

    /* Optional private free-text token (e.g. agreed with one specific friend to mark a
       deliberate beacon exchange, kept in a separate file from the whitelist/gates config --
       see .claude/state/context.md's Phase 3 config-plumbing entry). Empty string ("")
       disables the beacon-token path entirely -- a decode can then only match via the normal
       call_to/call_de-structured path below.

       ft8_lib's free-text message type carries no call_to/call_de fields at all (see
       argus.h's own note on argus_decode_t), so a beacon-token match can't check those
       structurally; it text-scans instead. The *default* (beacon_allow_token_alone == false)
       requires my_call and a whitelisted call to each appear as a whitespace-delimited token
       somewhere in the decoded free text, alongside the token itself (see cerberus.c's
       text_contains_token()) -- deliberately the stricter behavior by default, since Part 97
       requires station identification in every transmission and this keeps that spirit even
       on the beacon path, not just the ordinary structured exchange. */
    char beacon_token[CERBERUS_BEACON_TOKEN_MAX];

    /* Escape hatch: when true, the token alone authenticates and my_call/whitelist aren't
       text-scanned at all (useful when there's genuinely no room left in FT8's 13-character
       free text for both callsigns plus a distinguishing token). Deliberately not surfaced in
       any --help text or README -- the CLI flag that sets this stays undocumented by design,
       so flip it only via an explicit, out-of-band agreement with whoever operates this, not
       by discovery. Leave false unless there's a specific reason not to. */
    bool beacon_allow_token_alone;

    /* Gates -- each has its own has_*_gate flag so a config can enable only the ones it wants;
       a disabled gate is always considered satisfied. */
    bool has_band_gate;
    char band[CERBERUS_BAND_MAX]; /* e.g. "20m" -- matched against current_band below, not
                                      derived from the decode itself: argus.c/cerberus.c have
                                      no notion of which band the audio came from, only the
                                      caller (which knows the live CAT/dial frequency) does. */
    bool has_freq_gate;
    float freq_min_hz;
    float freq_max_hz; /* audio-frequency window, Hz -- compared against decode->freq_hz */
    bool has_snr_gate;
    float min_snr_db; /* compared against decode->snr_db */
} cerberus_config_t;

typedef struct {
    bool matched; /* true iff every configured predicate held */
    bool whitelist_ok;
    bool directed_at_me_ok;
    bool text_ok;
    bool gates_ok;
    bool is_beacon_token; /* true iff this decode matched via the beacon-token path rather
                              than a structured call_to/call_de exchange */
} cerberus_result_t;

/* current_band is the caller-supplied current band (see cerberus_config_t.band's own note).
   Pass NULL or "" when band gating isn't in use -- cfg->has_band_gate being false makes this
   parameter irrelevant either way, so a caller that never sets up band gating can always pass
   NULL safely.

   Every decode gets a result, matched or not -- "heard but not worked" is propagation
   evidence per the kickoff and must still be recorded by the caller (mnemosyne.c, a later
   phase); this function never discards a non-match, only reports it. */
cerberus_result_t cerberus_evaluate(const cerberus_config_t* cfg, const argus_decode_t* decode,
                                    const char* current_band);

#ifdef __cplusplus
}
#endif

#endif /* CERBERUS_H */

#ifndef QSO_H
#define QSO_H

#include <stdbool.h>
#include <stddef.h>
#include "argus.h"
#include "cerberus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Standard 6-message exchange (kickoff's own script), split by sender:
     A: CQ A GRID_A          (1, CQ)
     B: A B GRID_B            (2, grid)
     A: B A REPORT            (3, plain report of B)
     B: A B R+REPORT          (4, R-report of A)
     A: B A RR73               (5)
     B: A B 73                 (6)
   A sends 1/3/5, B sends 2/4/6. This module only implements confirm mode (Phase 3 scope) --
   it composes each reply and waits for cerberus.c/the caller to confirm before ever
   transmitting; qso_confirm_sent() only updates bookkeeping, the actual keying is Phase 4's
   job (core/atropos.c's PTT watchdog doesn't exist yet -- see .claude/CLAUDE.md's "never key
   the antenna first" ordering).

   "Both directions" (kickoff): which role I play is decided by which trigger starts the
   exchange, not hardcoded --
     - a whitelisted station's own CQ (heard via qso_on_cq_heard(), a separate check from
       cerberus_evaluate() since that predicate deliberately excludes CQs -- see
       .claude/state/context.md's Phase 3 entry) makes me role B, replying with my grid;
     - an inbound grid-shaped message already directed at me from a whitelisted station while
       idle (qso_on_decode() called with step==QSO_STEP_IDLE) makes me role A, replying with a
       report -- covers both "he's replying to a CQ I called" and "he's calling me directly
       with his grid", which look identical on the wire and are handled identically here;
       Phase 3 doesn't implement CQ transmission itself, so this module can't and doesn't try
       to distinguish those two cases. */

typedef enum {
    QSO_ROLE_NONE = 0,
    QSO_ROLE_A, /* I called CQ, or he replied to me directly with his grid */
    QSO_ROLE_B, /* I replied to his CQ */
} qso_role_t;

typedef enum {
    QSO_STEP_IDLE = 0,        /* no exchange in progress */
    QSO_STEP_PENDING_CONFIRM, /* pending_text composed, waiting for the operator's keypress */
    QSO_STEP_AWAITING_PEER,   /* my part sent, waiting for the peer's next message */
    QSO_STEP_COMPLETE,        /* both SNR directions captured; exchange formally done */
} qso_step_t;

/* Which reply pending_text holds while PENDING_CONFIRM, or which one was last sent while
   AWAITING_PEER (driving what qso_on_decode() expects to hear next). */
typedef enum {
    QSO_ACTION_NONE = 0,
    QSO_ACTION_GRID,
    QSO_ACTION_REPORT,
    QSO_ACTION_RREPORT,
    QSO_ACTION_RR73,
    QSO_ACTION_73,
} qso_action_t;

#define QSO_CALLSIGN_MAX 12
#define QSO_GRID_MAX 8
#define QSO_TEXT_MAX 32

typedef struct {
    char my_call[QSO_CALLSIGN_MAX];
    char my_grid[QSO_GRID_MAX];
} qso_config_t;

typedef struct {
    qso_step_t step;
    qso_role_t role;
    qso_action_t pending_action;
    char peer_call[QSO_CALLSIGN_MAX];
    char pending_text[QSO_TEXT_MAX];

    /* snr_i_sent -- report I gave him. snr_i_got -- report he gave me. Both become valid by
       the time the exchange reaches QSO_STEP_COMPLETE -- matches the kickoff's own `exchange`
       table (asymmetry_db = snr_i_got - snr_i_sent), which is the entire point of the
       project. Neither is transmitted or measured with meaningful precision below +/-1 dB or
       so; see argus.c's own snr_db calibration notes. */
    int snr_i_sent;
    bool snr_i_sent_valid;
    int snr_i_got;
    bool snr_i_got_valid;
} qso_t;

void qso_init(qso_t* qso);

/* Clears back to QSO_STEP_IDLE/QSO_ROLE_NONE -- call once the caller is done with a
   QSO_STEP_COMPLETE result (e.g. after logging it), to allow tracking a new exchange. */
void qso_reset(qso_t* qso);

/* Separate from cerberus_evaluate() (which deliberately excludes CQs, see cerberus.h) --
   checks whether `decode` is a CQ from a station on cerberus_cfg's whitelist. Only meaningful
   when qso->step == QSO_STEP_IDLE (already busy with someone otherwise); returns false and
   changes nothing if not idle or if the CQ isn't from a whitelisted station. On a true return,
   qso->step becomes QSO_STEP_PENDING_CONFIRM with pending_text composed (role B, grid reply)
   -- caller should display qso->pending_text and wait for the operator's confirm keypress. */
bool qso_on_cq_heard(qso_t* qso, const qso_config_t* cfg, const cerberus_config_t* cerberus_cfg,
                     const argus_decode_t* decode);

/* Feed one cerberus.c-matched decode (already confirmed directed at me, from the whitelisted
   peer) into the state machine. Advances state per the script above; when it composes a new
   reply, qso->step becomes QSO_STEP_PENDING_CONFIRM and returns true -- caller should display
   qso->pending_text and wait for the operator's confirm keypress. Returns false (no state
   change) if qso->step isn't QSO_STEP_AWAITING_PEER, if the decode isn't from qso->peer_call,
   or if the decode's shape doesn't match what's currently expected (e.g. a grid arrives when
   an RR73 was expected) -- a caller can safely feed every matched decode through this without
   pre-filtering by exchange step. */
bool qso_on_decode(qso_t* qso, const qso_config_t* cfg, const argus_decode_t* decode);

/* Operator pressed the confirm key -- qso->pending_text is assumed to now be transmitted (the
   actual keying is the caller's job, Phase 4+); advances step to QSO_STEP_AWAITING_PEER or,
   for the exchange's last message in either role (RR73 for role A, 73 for role B -- both SNR
   directions are already captured by then), straight to QSO_STEP_COMPLETE. Only valid when
   step == QSO_STEP_PENDING_CONFIRM. */
void qso_confirm_sent(qso_t* qso);

/* The ~2s confirm window elapsed with no keypress. Per the kickoff: "a missed confirmation
   must skip the slot and re-offer, never transmit late" -- this deliberately leaves
   qso->pending_text and step untouched (still QSO_STEP_PENDING_CONFIRM) so the same offer is
   simply re-presented at the next opportunity; it exists as an explicit, named call so a
   confirm-mode caller's intent (as opposed to just doing nothing) is visible at the call site,
   and as a natural place for future missed-confirm telemetry. */
void qso_confirm_missed(qso_t* qso);

#ifdef __cplusplus
}
#endif

#endif /* QSO_H */

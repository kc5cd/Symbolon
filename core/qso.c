/* QSO -- plain functional name, not one of the mythically-named modules (see the kickoff's
   table and .claude/CLAUDE.md's "mythically-named files" note). */
#include "qso.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void compose(char* out, size_t max_len, const char* to, const char* de, const char* extra)
{
    snprintf(out, max_len, "%s %s %s", to, de, extra);
}

/* Signed 2-digit report field, e.g. "-12", "+05" -- matches the sign-plus-2-digit shape every
   report in the kickoff's own example uses ("-12", "R-08"); no example with a positive value
   is given, so the "+" prefix on non-negative reports is this implementation's own
   (defensible, standard-FT8-report-field-shaped) choice, not verified against a real
   positive-SNR exchange yet. */
static void format_report(int snr_db, char* out, size_t max_len)
{
    if (snr_db >= 0) {
        snprintf(out, max_len, "+%02d", snr_db);
    } else {
        snprintf(out, max_len, "-%02d", -snr_db);
    }
}

static void format_rreport(int snr_db, char* out, size_t max_len)
{
    char report[8];
    format_report(snr_db, report, sizeof(report));
    snprintf(out, max_len, "R%s", report);
}

/* True iff `s` starts with 2 uppercase letters + 2 digits (a Maidenhead grid square's
   mandatory 4-character prefix; the optional 6-character extension isn't checked since the
   kickoff's own examples only ever use 4-character grids). */
static bool is_grid_shaped(const char* s)
{
    return s[0] != '\0' && isupper((unsigned char)s[0])
        && isupper((unsigned char)s[1])
        && isdigit((unsigned char)s[2])
        && isdigit((unsigned char)s[3])
        && s[4] == '\0';
}

/* True iff `s` is a plain (no "R" prefix) signed 2-digit report, e.g. "-12", "+05". */
static bool is_plain_report_shaped(const char* s)
{
    return (s[0] == '+' || s[0] == '-')
        && isdigit((unsigned char)s[1])
        && isdigit((unsigned char)s[2])
        && s[3] == '\0';
}

static bool is_r_report_shaped(const char* s)
{
    return s[0] == 'R' && is_plain_report_shaped(s + 1);
}

static int parse_report(const char* s)
{
    if (s[0] == 'R') {
        ++s;
    }
    return atoi(s);
}

static int round_snr_db(float snr_db)
{
    return (int)(snr_db < 0 ? snr_db - 0.5f : snr_db + 0.5f);
}

void qso_init(qso_t* qso)
{
    memset(qso, 0, sizeof(*qso));
}

void qso_reset(qso_t* qso)
{
    memset(qso, 0, sizeof(*qso));
}

bool qso_on_cq_heard(qso_t* qso, const qso_config_t* cfg, const cerberus_config_t* cerberus_cfg,
                     const argus_decode_t* decode)
{
    if (qso->step != QSO_STEP_IDLE) {
        return false;
    }
    if (strcmp(decode->call_to, "CQ") != 0 || decode->call_de[0] == '\0') {
        return false;
    }
    bool whitelisted = false;
    for (int i = 0; i < cerberus_cfg->whitelist_count && !whitelisted; ++i) {
        whitelisted = (strcmp(cerberus_cfg->whitelist[i], decode->call_de) == 0);
    }
    if (!whitelisted) {
        return false;
    }

    qso->role = QSO_ROLE_B;
    strncpy(qso->peer_call, decode->call_de, sizeof(qso->peer_call) - 1);
    compose(qso->pending_text, sizeof(qso->pending_text), qso->peer_call, cfg->my_call, cfg->my_grid);
    qso->pending_action = QSO_ACTION_GRID;
    qso->step = QSO_STEP_PENDING_CONFIRM;
    return true;
}

bool qso_on_decode(qso_t* qso, const qso_config_t* cfg, const argus_decode_t* decode)
{
    if (decode->call_to[0] == '\0' || strcmp(decode->call_to, cfg->my_call) != 0) {
        return false;
    }

    if (qso->step == QSO_STEP_IDLE) {
        /* Entry point for role A -- see qso.h's own note on why this covers both "he's
           replying to a CQ I called" and "he's calling me directly": they're indistinguishable
           on the wire without Phase 3 also tracking whether I transmitted a CQ, which it
           doesn't. */
        if (!is_grid_shaped(decode->extra) || decode->call_de[0] == '\0') {
            return false;
        }
        qso->role = QSO_ROLE_A;
        strncpy(qso->peer_call, decode->call_de, sizeof(qso->peer_call) - 1);
        int snr = round_snr_db(decode->snr_db);
        qso->snr_i_sent = snr;
        qso->snr_i_sent_valid = true;
        char report[8];
        format_report(snr, report, sizeof(report));
        compose(qso->pending_text, sizeof(qso->pending_text), qso->peer_call, cfg->my_call, report);
        qso->pending_action = QSO_ACTION_REPORT;
        qso->step = QSO_STEP_PENDING_CONFIRM;
        return true;
    }

    if (qso->step != QSO_STEP_AWAITING_PEER || strcmp(decode->call_de, qso->peer_call) != 0) {
        return false;
    }

    switch (qso->pending_action) {
    case QSO_ACTION_GRID:
        /* Role B, waiting for A's plain report of me. */
        if (!is_plain_report_shaped(decode->extra)) {
            return false;
        }
        qso->snr_i_got = parse_report(decode->extra);
        qso->snr_i_got_valid = true;
        {
            int snr = round_snr_db(decode->snr_db);
            qso->snr_i_sent = snr;
            qso->snr_i_sent_valid = true;
            char rreport[8];
            format_rreport(snr, rreport, sizeof(rreport));
            compose(qso->pending_text, sizeof(qso->pending_text), qso->peer_call, cfg->my_call, rreport);
        }
        qso->pending_action = QSO_ACTION_RREPORT;
        qso->step = QSO_STEP_PENDING_CONFIRM;
        return true;

    case QSO_ACTION_REPORT:
        /* Role A, waiting for B's R-report of me. */
        if (!is_r_report_shaped(decode->extra)) {
            return false;
        }
        qso->snr_i_got = parse_report(decode->extra);
        qso->snr_i_got_valid = true;
        compose(qso->pending_text, sizeof(qso->pending_text), qso->peer_call, cfg->my_call, "RR73");
        qso->pending_action = QSO_ACTION_RR73;
        qso->step = QSO_STEP_PENDING_CONFIRM;
        return true;

    case QSO_ACTION_RREPORT:
        /* Role B, waiting for A's RR73. */
        if (strcmp(decode->extra, "RR73") != 0) {
            return false;
        }
        compose(qso->pending_text, sizeof(qso->pending_text), qso->peer_call, cfg->my_call, "73");
        qso->pending_action = QSO_ACTION_73;
        qso->step = QSO_STEP_PENDING_CONFIRM;
        return true;

    default:
        /* QSO_ACTION_RR73/QSO_ACTION_73/QSO_ACTION_NONE: both roles' exchanges complete
           straight from qso_confirm_sent() (see there) without an AWAITING_PEER step of
           their own -- both SNR directions are already known by then, so there's nothing left
           for qso_on_decode() to wait for. A stray decode reaching here (e.g. a courtesy "73"
           after I've already marked complete) is a no-op, not an error. */
        return false;
    }
}

void qso_confirm_sent(qso_t* qso)
{
    if (qso->step != QSO_STEP_PENDING_CONFIRM) {
        return;
    }

    switch (qso->pending_action) {
    case QSO_ACTION_RR73: /* role A's last message -- both SNR directions already known */
    case QSO_ACTION_73:   /* role B's last message -- likewise */
        qso->step = QSO_STEP_COMPLETE;
        break;
    default:
        qso->step = QSO_STEP_AWAITING_PEER;
        break;
    }
}

void qso_confirm_missed(qso_t* qso)
{
    (void)qso; /* deliberately a no-op -- see qso.h's own note */
}

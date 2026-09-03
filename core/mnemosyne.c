/* Mnemosyne -- see mnemosyne.h for the myth note. */
#include "mnemosyne.h"

#include <string.h>

void mnemosyne_init(mnemosyne_t* m, const mnemosyne_sink_t* sink)
{
    memset(m, 0, sizeof(*m));
    m->sink = *sink;
}

void mnemosyne_slot_reset(mnemosyne_t* m)
{
    m->dedup_count = 0;
}

static bool already_logged_this_slot(mnemosyne_t* m, const char* call_de, const char* text)
{
    for (int i = 0; i < m->dedup_count; ++i) {
        if (strcmp(m->dedup_call[i], call_de) == 0 && strcmp(m->dedup_text[i], text) == 0) {
            return true;
        }
    }
    return false;
}

void mnemosyne_observe(mnemosyne_t* m, const argus_decode_t* decode, const cerberus_result_t* result,
                        const char* current_band, uint64_t utc_us)
{
    if (!result->whitelist_ok) {
        return;
    }
    if (already_logged_this_slot(m, decode->call_de, decode->text)) {
        return;
    }
    if (m->dedup_count >= MNEMOSYNE_DEDUP_CAPACITY) {
        /* Dedup set full for this slot -- drop rather than risk a double-logged row, see
           mnemosyne.h's own note. */
        return;
    }

    strncpy(m->dedup_call[m->dedup_count], decode->call_de, sizeof(m->dedup_call[0]) - 1);
    m->dedup_call[m->dedup_count][sizeof(m->dedup_call[0]) - 1] = '\0';
    strncpy(m->dedup_text[m->dedup_count], decode->text, sizeof(m->dedup_text[0]) - 1);
    m->dedup_text[m->dedup_count][sizeof(m->dedup_text[0]) - 1] = '\0';
    m->dedup_count++;

    mnemosyne_observation_t obs;
    memset(&obs, 0, sizeof(obs));
    obs.utc_us = utc_us;
    strncpy(obs.call_de, decode->call_de, sizeof(obs.call_de) - 1);
    strncpy(obs.call_to, decode->call_to, sizeof(obs.call_to) - 1);
    if (current_band != NULL) {
        strncpy(obs.band, current_band, sizeof(obs.band) - 1);
    }
    strncpy(obs.text, decode->text, sizeof(obs.text) - 1);
    obs.freq_hz = decode->freq_hz;
    obs.snr_db = decode->snr_db;
    obs.is_beacon_token = result->is_beacon_token;

    if (m->sink.on_observation != NULL) {
        m->sink.on_observation(m->sink.user, &obs);
    }
}

void mnemosyne_log_qso(mnemosyne_t* m, const qso_t* qso, const qso_config_t* cfg, uint64_t utc_us)
{
    if (!qso->snr_i_sent_valid || !qso->snr_i_got_valid) {
        return;
    }

    mnemosyne_qso_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.utc_us = utc_us;
    strncpy(rec.my_call, cfg->my_call, sizeof(rec.my_call) - 1);
    strncpy(rec.peer_call, qso->peer_call, sizeof(rec.peer_call) - 1);
    rec.snr_i_sent = qso->snr_i_sent;
    rec.snr_i_got = qso->snr_i_got;
    rec.asymmetry_db = qso->snr_i_got - qso->snr_i_sent;

    if (m->sink.on_qso_complete != NULL) {
        m->sink.on_qso_complete(m->sink.user, &rec);
    }
}

/* Atropos -- see atropos.h for the myth note. */
#include "atropos.h"

#include <string.h>

static uint64_t now_us(const atropos_t* atropos)
{
    return atropos->host.mono_us(atropos->host.user);
}

void atropos_init(atropos_t* atropos, const atropos_config_t* config, const sym_host_t* host)
{
    memset(atropos, 0, sizeof(*atropos));
    atropos->config = *config;
    atropos->host = *host;
}

bool atropos_freq_allowed(const atropos_t* atropos, uint64_t freq_hz)
{
    for (int i = 0; i < atropos->config.allowed_freq_count; ++i) {
        uint64_t allowed = atropos->config.allowed_freq_hz[i];
        uint64_t tol = atropos->config.freq_tolerance_hz;
        uint64_t lo = (allowed > tol) ? (allowed - tol) : 0;
        uint64_t hi = allowed + tol;
        if (freq_hz >= lo && freq_hz <= hi) {
            return true;
        }
    }
    return false;
}

bool atropos_tx_budget_ok(const atropos_t* atropos)
{
    if (atropos->config.max_tx_us_session > 0
        && atropos->total_tx_us_session >= atropos->config.max_tx_us_session) {
        return false;
    }

    if (atropos->config.max_tx_slots_per_hour > 0) {
        uint64_t now = now_us(atropos);
        const uint64_t kHourUs = 3600ULL * 1000000ULL;
        uint32_t valid_entries = (atropos->tx_slot_count < ATROPOS_MAX_TX_SLOT_HISTORY)
            ? atropos->tx_slot_count
            : ATROPOS_MAX_TX_SLOT_HISTORY;

        int count_in_last_hour = 0;
        for (uint32_t i = 0; i < valid_entries; ++i) {
            if (now - atropos->tx_slot_start_us[i] < kHourUs) {
                ++count_in_last_hour;
            }
        }
        if (count_in_last_hour >= atropos->config.max_tx_slots_per_hour) {
            return false;
        }
    }

    return true;
}

void atropos_ptt_asserted(atropos_t* atropos)
{
    uint64_t now = now_us(atropos);
    atropos->ptt_asserted = true;
    atropos->ptt_asserted_at_us = now;

    uint32_t idx = atropos->tx_slot_count % ATROPOS_MAX_TX_SLOT_HISTORY;
    atropos->tx_slot_start_us[idx] = now;
    ++atropos->tx_slot_count;
}

void atropos_ptt_released(atropos_t* atropos)
{
    if (!atropos->ptt_asserted) {
        return;
    }
    uint64_t now = now_us(atropos);
    atropos->total_tx_us_session += (now - atropos->ptt_asserted_at_us);
    atropos->ptt_asserted = false;
}

bool atropos_watchdog_tick(atropos_t* atropos)
{
    if (!atropos->ptt_asserted) {
        return false;
    }
    uint64_t now = now_us(atropos);
    if (now - atropos->ptt_asserted_at_us < atropos->config.ptt_watchdog_us) {
        return false;
    }

    /* Overdue -- force off. Best-effort: even if the callback itself reports failure, this
       module's own bookkeeping still treats PTT as released (retrying indefinitely isn't
       this module's job; a caller/host that wants failure escalation handles that itself). */
    (void)atropos->host.ptt_set(atropos->host.user, 0);
    atropos->total_tx_us_session += (now - atropos->ptt_asserted_at_us);
    atropos->ptt_asserted = false;
    return true;
}

void atropos_operator_input(atropos_t* atropos)
{
    atropos->last_operator_input_us = now_us(atropos);
}

bool atropos_dead_man_tick(atropos_t* atropos)
{
    if (!atropos->armed || atropos->config.dead_man_timeout_us == 0) {
        return false;
    }
    uint64_t now = now_us(atropos);
    if (now - atropos->last_operator_input_us >= atropos->config.dead_man_timeout_us) {
        atropos->armed = false;
        return true;
    }
    return false;
}

void atropos_arm(atropos_t* atropos)
{
    atropos->armed = true;
    atropos->last_operator_input_us = now_us(atropos);
}

void atropos_disarm(atropos_t* atropos)
{
    atropos->armed = false;
}

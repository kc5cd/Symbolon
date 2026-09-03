/* Cerberus -- see cerberus.h for the myth note. */
#include "cerberus.h"

#include <string.h>

/* Whitespace-delimited exact match: true iff `token` appears as a whole space-separated word
   in `text`, not merely as a substring (so a callsign like "KC5CD" doesn't spuriously match
   inside a longer word). FT8 message fields are always single-space-joined by ft8_lib's own
   ftx_message_decode() (see message.c's append_string() calls), so this is the right
   granularity for both callsigns and the beacon token. */
static bool text_contains_token(const char* text, const char* token)
{
    if (token == NULL || token[0] == '\0') {
        return false;
    }
    size_t token_len = strlen(token);

    const char* p = text;
    while (*p != '\0') {
        while (*p == ' ') {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        const char* word_start = p;
        while (*p != '\0' && *p != ' ') {
            ++p;
        }
        size_t word_len = (size_t)(p - word_start);
        if (word_len == token_len && strncmp(word_start, token, token_len) == 0) {
            return true;
        }
    }
    return false;
}

static bool whitelist_contains(const cerberus_config_t* cfg, const char* call)
{
    if (call == NULL || call[0] == '\0') {
        return false;
    }
    for (int i = 0; i < cfg->whitelist_count; ++i) {
        if (strcmp(cfg->whitelist[i], call) == 0) {
            return true;
        }
    }
    return false;
}

static bool gates_satisfied(const cerberus_config_t* cfg, const argus_decode_t* decode, const char* current_band)
{
    if (cfg->has_band_gate) {
        if (current_band == NULL || strcmp(cfg->band, current_band) != 0) {
            return false;
        }
    }
    if (cfg->has_freq_gate) {
        if (decode->freq_hz < cfg->freq_min_hz || decode->freq_hz > cfg->freq_max_hz) {
            return false;
        }
    }
    if (cfg->has_snr_gate) {
        if (decode->snr_db < cfg->min_snr_db) {
            return false;
        }
    }
    return true;
}

cerberus_result_t cerberus_evaluate(const cerberus_config_t* cfg, const argus_decode_t* decode,
                                    const char* current_band)
{
    cerberus_result_t result;
    memset(&result, 0, sizeof(result));

    bool beacon_token_present = (cfg->beacon_token[0] != '\0') && text_contains_token(decode->text, cfg->beacon_token);

    if (beacon_token_present) {
        result.is_beacon_token = true;
        result.text_ok = true;
        if (cfg->beacon_allow_token_alone) {
            /* Undocumented escape hatch -- see cerberus.h's own note. The token's own secrecy
               plus confirm mode's human keypress gate are the authentication here. */
            result.directed_at_me_ok = true;
            result.whitelist_ok = true;
        } else {
            /* Default: still require station identification in the message, matching Part 97
               even on the beacon path -- see cerberus.h's own note. */
            result.directed_at_me_ok = text_contains_token(decode->text, cfg->my_call);
            /* Any whitelisted call appearing as a token is enough -- matches the non-beacon
               path's "call_de is one of the whitelist" semantics, just via text-scanning
               since a free-text message has no structured call_de to check directly. */
            result.whitelist_ok = false;
            for (int i = 0; i < cfg->whitelist_count && !result.whitelist_ok; ++i) {
                result.whitelist_ok = text_contains_token(decode->text, cfg->whitelist[i]);
            }
        }
    } else {
        /* Structured path: call_to/call_de are only populated when argus_decode_slot()
           actually resolved a STANDARD or NONSTD_CALL message type (see argus.c) -- an
           unresolved hashed callsign ("<...>", not previously seen this session), a free-text
           message with no beacon token, or telemetry all leave these empty. Reaching this
           decode with both fields non-empty already means it decoded as a recognized exchange
           message type, satisfying the "message type" half of the kickoff's text predicate --
           see cerberus.h's own note on how the beacon token supplies the other half ("literal
           content"). Whitelist/directed-at-me below would fail on empty fields regardless
           (neither "" nor "<...>" is ever a real callsign), so this doesn't change `matched`
           -- it just keeps the per-predicate diagnostic honest for a genuinely unstructured
           decode instead of reporting text_ok=true for something that matched nothing. */
        result.text_ok = (decode->call_to[0] != '\0') && (decode->call_de[0] != '\0');
        result.whitelist_ok = whitelist_contains(cfg, decode->call_de);
        result.directed_at_me_ok = (decode->call_to[0] != '\0') && (strcmp(decode->call_to, cfg->my_call) == 0);
    }

    result.gates_ok = gates_satisfied(cfg, decode, current_band);
    result.matched = result.whitelist_ok && result.directed_at_me_ok && result.text_ok && result.gates_ok;
    return result;
}

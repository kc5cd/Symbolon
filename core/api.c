#include "api.h"
#include <string.h>

/* No commands are registered yet -- rules/QSO/store commands arrive with the phases that
   implement them (3, 5). This table's only job right now is to prove the tier-filtering
   seam works, per Phase 6's eventual test gate: "assert higher-tier tools are absent from
   tools/list at each --mcp-tier setting". */
static const sym_command_t s_commands[] = {
    { NULL, SYM_TIER_OBSERVE, NULL } /* sentinel; never matched, count stays 0 below */
};
static const size_t s_command_count = 0;

size_t sym_command_list(sym_tier_t tier, const sym_command_t* out, size_t max_out)
{
    size_t written = 0;
    for (size_t i = 0; i < s_command_count; ++i) {
        if (s_commands[i].min_tier > tier) {
            continue;
        }
        if (out != NULL && written < max_out) {
            /* NOLINTNEXTLINE: writing into caller-owned array, not s_commands itself */
            ((sym_command_t*)out)[written] = s_commands[i];
        }
        ++written;
    }
    return written;
}

sym_rc_t sym_dispatch(sym_ctx_t* ctx, sym_tier_t caller_tier, const char* command_name,
                      const sym_args_t* args, sym_result_t* result)
{
    if (command_name == NULL) {
        return SYM_RC_INVALID_ARG;
    }
    for (size_t i = 0; i < s_command_count; ++i) {
        if (strcmp(s_commands[i].name, command_name) != 0) {
            continue;
        }
        if (s_commands[i].min_tier > caller_tier) {
            return SYM_RC_DENIED;
        }
        return s_commands[i].invoke(ctx, args, result);
    }
    return SYM_RC_INVALID_ARG;
}

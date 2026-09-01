#ifndef SYM_API_H
#define SYM_API_H

#include <stddef.h>
#include "sym_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* THE MCP SEAM. Both the CLI (always full-privilege, per the kickoff) and the future MCP
   server (Phase 6, constrained by --mcp-tier) call sym_dispatch(). Every command declares
   the minimum tier it requires, so the gate has exactly one implementation.

   Ordering is deliberate: orchestrate ranks below transmit, because a scripted band sweep
   has a far narrower blast radius than arbitrary keying. */
typedef enum {
    SYM_TIER_OBSERVE = 0,   /* read decodes, SNR history, rig state, log */
    SYM_TIER_CONTROL,       /* + QSY, mode, whitelist/rules, arm/disarm  */
    SYM_TIER_ORCHESTRATE,   /* + run a scripted band sweep               */
    SYM_TIER_TRANSMIT,      /* + compose and key an arbitrary message    */
} sym_tier_t;

/* Opaque: shape settled per-command as rules/QSO/store commands are implemented in later
   phases. Nothing in Phase 0 constructs one. */
typedef struct sym_ctx_t sym_ctx_t;
typedef struct sym_args_t sym_args_t;
typedef struct sym_result_t sym_result_t;

typedef struct {
    const char* name;
    sym_tier_t  min_tier;
    sym_rc_t  (*invoke)(sym_ctx_t*, const sym_args_t*, sym_result_t*);
} sym_command_t;

/* Filters the advertised command list to those with min_tier <= tier. Commands above the
   selected tier are never registered in the returned list, not registered-and-refused -- an
   unadvertised command can't be talked into firing. Returns the number of commands written
   to out (out may be sized via a NULL-out, 0-max_out probing call, matching snprintf's
   convention: returns the count that *would* be written). */
size_t sym_command_list(sym_tier_t tier, const sym_command_t* out, size_t max_out);

/* Re-checks the tier on every invoke regardless of what sym_command_list() already filtered
   out -- the two checks are independent on purpose. Returns SYM_RC_DENIED if command_name's
   min_tier exceeds caller_tier, SYM_RC_INVALID_ARG if command_name is not registered. */
sym_rc_t sym_dispatch(sym_ctx_t* ctx, sym_tier_t caller_tier, const char* command_name,
                      const sym_args_t* args, sym_result_t* result);

#ifdef __cplusplus
}
#endif

#endif /* SYM_API_H */

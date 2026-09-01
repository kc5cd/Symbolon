#include "hal_cat.h"
#include <stddef.h>

/* Phase 0 stub -- proves the seam links on both platforms. Hamlib isn't linked yet; the
   real RIG_MODEL_X6200-backed implementation lands in Phase 2, per the kickoff's phasing
   table. No Windows Hamlib dev SDK has been located on this machine yet either (see the
   Phase 0 plan's toolchain findings) -- Phase 2 needs to resolve that before this can call
   real Hamlib APIs. */

struct hal_cat {
    int unused;
};

hal_rc_t hal_cat_open(hal_cat_t** out, const hal_cat_config_t* config)
{
    (void)config;
    if (out != NULL) {
        *out = NULL;
    }
    return HAL_RC_UNSUPPORTED;
}

void hal_cat_close(hal_cat_t* cat)
{
    (void)cat;
}

hal_rc_t hal_cat_set_freq_hz(hal_cat_t* cat, uint64_t hz)
{
    (void)cat;
    (void)hz;
    return HAL_RC_UNSUPPORTED;
}

hal_rc_t hal_cat_get_freq_hz(hal_cat_t* cat, uint64_t* out_hz)
{
    (void)cat;
    if (out_hz != NULL) {
        *out_hz = 0;
    }
    return HAL_RC_UNSUPPORTED;
}

hal_rc_t hal_cat_set_mode(hal_cat_t* cat, hal_cat_mode_t mode)
{
    (void)cat;
    (void)mode;
    return HAL_RC_UNSUPPORTED;
}

hal_rc_t hal_cat_get_mode(hal_cat_t* cat, hal_cat_mode_t* out_mode)
{
    (void)cat;
    if (out_mode != NULL) {
        *out_mode = HAL_CAT_MODE_USB;
    }
    return HAL_RC_UNSUPPORTED;
}

hal_rc_t hal_cat_set_ptt(hal_cat_t* cat, bool assert_tx)
{
    (void)cat;
    (void)assert_tx;
    return HAL_RC_UNSUPPORTED;
}

hal_rc_t hal_cat_get_ptt(hal_cat_t* cat, bool* out_asserted)
{
    (void)cat;
    if (out_asserted != NULL) {
        *out_asserted = false;
    }
    return HAL_RC_UNSUPPORTED;
}

const char* hal_cat_last_error(hal_cat_t* cat)
{
    (void)cat;
    return "hal_cat: not yet implemented (Phase 0 stub)";
}

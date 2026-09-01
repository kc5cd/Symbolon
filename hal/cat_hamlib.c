#include "hal_cat.h"

#include <hamlib/rig.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Phase 2 -- real RIG_MODEL_X6200-backed implementation, per the kickoff's phasing table.
   hal_cat.h's own comment covers the seam rationale; this file is just the Hamlib side of
   it. Nothing here keys PTT on real hardware by itself -- that's core/atropos.c's call, made
   through hal_cat_set_ptt(), same as any other caller. */

struct hal_cat {
    RIG* rig;
    char last_error[256];
};

/* rig_init() needs the target model's backend already registered, or it returns NULL --
   every Hamlib client (rigctl, WSJT-X, fldigi) calls this once at startup for exactly that
   reason. Guarded so a process that opens more than one hal_cat_t only pays for it once. */
static void ensure_backends_loaded(void)
{
    static bool s_loaded = false;
    if (!s_loaded) {
        /* Set before rig_load_all_backends(), not after -- every backend's own _init()
           logs at a level below RIG_DEBUG_ERR, and that call is what triggers all of them
           at once. hal/ is allowed OS calls (only core/ isn't), but silent-by-default is
           still the right default for a CLI tool -- errors only. */
        rig_set_debug(RIG_DEBUG_ERR);
        rig_load_all_backends();
        s_loaded = true;
    }
}

static hal_rc_t record_hamlib_error(struct hal_cat* cat, const char* what, int hamlib_rc)
{
    snprintf(cat->last_error, sizeof(cat->last_error), "%s: %s", what, rigerror(hamlib_rc));
    return HAL_RC_ERROR;
}

static rmode_t hal_mode_to_hamlib(hal_cat_mode_t mode)
{
    switch (mode) {
    case HAL_CAT_MODE_LSB:
        return RIG_MODE_LSB;
    case HAL_CAT_MODE_DATA_U:
        return RIG_MODE_PKTUSB;
    case HAL_CAT_MODE_DATA_L:
        return RIG_MODE_PKTLSB;
    case HAL_CAT_MODE_CW:
        return RIG_MODE_CW;
    case HAL_CAT_MODE_USB:
    default:
        return RIG_MODE_USB;
    }
}

static hal_cat_mode_t hamlib_mode_to_hal(rmode_t mode)
{
    switch (mode) {
    case RIG_MODE_LSB:
        return HAL_CAT_MODE_LSB;
    case RIG_MODE_PKTUSB:
        return HAL_CAT_MODE_DATA_U;
    case RIG_MODE_PKTLSB:
        return HAL_CAT_MODE_DATA_L;
    case RIG_MODE_CW:
        return HAL_CAT_MODE_CW;
    case RIG_MODE_USB:
    default:
        return HAL_CAT_MODE_USB;
    }
}

hal_rc_t hal_cat_open(hal_cat_t** out, const hal_cat_config_t* config)
{
    if (out == NULL || config == NULL) {
        return HAL_RC_INVALID_ARG;
    }
    *out = NULL;

    ensure_backends_loaded();

    struct hal_cat* cat = (struct hal_cat*)calloc(1, sizeof(struct hal_cat));
    if (cat == NULL) {
        return HAL_RC_ERROR;
    }

    cat->rig = rig_init((rig_model_t)config->rig_model);
    if (cat->rig == NULL) {
        /* No handle exists yet to carry a last_error string on -- matches
           hal_audio_open()'s same "message discarded on an open-time allocation failure"
           shape in hal/audio_miniaudio.c. */
        free(cat);
        return HAL_RC_ERROR;
    }

    if (config->port != NULL) {
        strncpy(cat->rig->state.rigport.pathname, config->port, HAMLIB_FILPATHLEN - 1);
        cat->rig->state.rigport.pathname[HAMLIB_FILPATHLEN - 1] = '\0';
    }
    if (config->baud != 0) {
        cat->rig->state.rigport.parm.serial.rate = (int)config->baud;
    }
    if (config->timeout_ms != 0) {
        cat->rig->state.rigport.timeout = (int)config->timeout_ms;
    }

    int rc = rig_open(cat->rig);
    if (rc != RIG_OK) {
        rig_cleanup(cat->rig);
        free(cat);
        return HAL_RC_ERROR;
    }

    *out = (hal_cat_t*)cat;
    return HAL_RC_OK;
}

void hal_cat_close(hal_cat_t* cat_handle)
{
    struct hal_cat* cat = (struct hal_cat*)cat_handle;
    if (cat == NULL) {
        return;
    }
    if (cat->rig != NULL) {
        rig_close(cat->rig);
        rig_cleanup(cat->rig);
    }
    free(cat);
}

hal_rc_t hal_cat_set_freq_hz(hal_cat_t* cat_handle, uint64_t hz)
{
    struct hal_cat* cat = (struct hal_cat*)cat_handle;
    if (cat == NULL) {
        return HAL_RC_INVALID_ARG;
    }
    int rc = rig_set_freq(cat->rig, RIG_VFO_CURR, (freq_t)hz);
    return (rc == RIG_OK) ? HAL_RC_OK : record_hamlib_error(cat, "rig_set_freq", rc);
}

hal_rc_t hal_cat_get_freq_hz(hal_cat_t* cat_handle, uint64_t* out_hz)
{
    struct hal_cat* cat = (struct hal_cat*)cat_handle;
    if (cat == NULL || out_hz == NULL) {
        return HAL_RC_INVALID_ARG;
    }
    freq_t freq = 0.0;
    int rc = rig_get_freq(cat->rig, RIG_VFO_CURR, &freq);
    if (rc != RIG_OK) {
        *out_hz = 0;
        return record_hamlib_error(cat, "rig_get_freq", rc);
    }
    *out_hz = (uint64_t)freq;
    return HAL_RC_OK;
}

hal_rc_t hal_cat_set_mode(hal_cat_t* cat_handle, hal_cat_mode_t mode)
{
    struct hal_cat* cat = (struct hal_cat*)cat_handle;
    if (cat == NULL) {
        return HAL_RC_INVALID_ARG;
    }
    int rc = rig_set_mode(cat->rig, RIG_VFO_CURR, hal_mode_to_hamlib(mode), RIG_PASSBAND_NORMAL);
    return (rc == RIG_OK) ? HAL_RC_OK : record_hamlib_error(cat, "rig_set_mode", rc);
}

hal_rc_t hal_cat_get_mode(hal_cat_t* cat_handle, hal_cat_mode_t* out_mode)
{
    struct hal_cat* cat = (struct hal_cat*)cat_handle;
    if (cat == NULL || out_mode == NULL) {
        return HAL_RC_INVALID_ARG;
    }
    rmode_t mode = RIG_MODE_NONE;
    pbwidth_t width = 0;
    int rc = rig_get_mode(cat->rig, RIG_VFO_CURR, &mode, &width);
    if (rc != RIG_OK) {
        *out_mode = HAL_CAT_MODE_USB;
        return record_hamlib_error(cat, "rig_get_mode", rc);
    }
    *out_mode = hamlib_mode_to_hal(mode);
    return HAL_RC_OK;
}

hal_rc_t hal_cat_set_ptt(hal_cat_t* cat_handle, bool assert_tx)
{
    struct hal_cat* cat = (struct hal_cat*)cat_handle;
    if (cat == NULL) {
        return HAL_RC_INVALID_ARG;
    }
    int rc = rig_set_ptt(cat->rig, RIG_VFO_CURR, assert_tx ? RIG_PTT_ON : RIG_PTT_OFF);
    return (rc == RIG_OK) ? HAL_RC_OK : record_hamlib_error(cat, "rig_set_ptt", rc);
}

hal_rc_t hal_cat_get_ptt(hal_cat_t* cat_handle, bool* out_asserted)
{
    struct hal_cat* cat = (struct hal_cat*)cat_handle;
    if (cat == NULL || out_asserted == NULL) {
        return HAL_RC_INVALID_ARG;
    }
    ptt_t ptt = RIG_PTT_OFF;
    int rc = rig_get_ptt(cat->rig, RIG_VFO_CURR, &ptt);
    if (rc != RIG_OK) {
        *out_asserted = false;
        return record_hamlib_error(cat, "rig_get_ptt", rc);
    }
    *out_asserted = (ptt != RIG_PTT_OFF);
    return HAL_RC_OK;
}

const char* hal_cat_last_error(hal_cat_t* cat_handle)
{
    struct hal_cat* cat = (struct hal_cat*)cat_handle;
    if (cat == NULL) {
        return "hal_cat: null handle";
    }
    return (cat->last_error[0] != '\0') ? cat->last_error : "no error";
}

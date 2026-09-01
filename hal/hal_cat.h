#ifndef HAL_CAT_H
#define HAL_CAT_H

#include <stdbool.h>
#include <stdint.h>
#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Implemented by hal/cat_hamlib.c (Windows + Linux, via Hamlib RIG_MODEL_X6200) or
   hal/cat_x6200.c (radio-native -- on the radio there is no CAT, you talk to the backend
   directly, and nobody has documented that yet; every entry point returns
   HAL_RC_UNSUPPORTED). The seam exists from commit one so that port is a new file, not a
   refactor -- see the kickoff's "genuine unknown" note.

   Phase 0 ships this header only; hal_cat.h has no core-facing struct until Phase 2 wires
   in Hamlib, since core/qso.c and core/atropos.c don't touch CAT directly -- app/ owns the
   hal_cat_t handle and feeds core only what it needs (frequency, PTT state) through
   sym_host_t and the ring/tx interfaces. */

typedef struct hal_cat hal_cat_t;

typedef enum {
    HAL_CAT_MODE_USB = 0, /* FT8 always transmits on USB dial frequency, per the kickoff */
    HAL_CAT_MODE_LSB,
    HAL_CAT_MODE_DATA_U,
    HAL_CAT_MODE_DATA_L,
    HAL_CAT_MODE_CW,
} hal_cat_mode_t;

typedef enum {
    HAL_CAT_AGC_OFF = 0,
    HAL_CAT_AGC_SLOW,
    HAL_CAT_AGC_FAST,
    HAL_CAT_AGC_AUTO,
} hal_cat_agc_t;

typedef struct {
    const char* port;      /* "COM5" / "/dev/ttyACM0"; NULL = Hamlib's own default */
    uint32_t    baud;      /* 19200 -- X6200 SERIAL-B via the CH342 USB bridge */
    int         rig_model; /* RIG_MODEL_X6200 (Hamlib rigs/icom/xiegu.c) */
    uint32_t    timeout_ms;

    /* hal_cat_set/get_power_watts() normally convert through Hamlib's own
       rig_mW2power()/rig_power2mW(), which look up the backend's tx_range_list for the
       current freq/mode. Confirmed empirically against real X6200 hardware (2026-09-01,
       see .claude/state/context.md) that RIG_MODEL_X6200's tx_range_list is empty -- Hamlib
       silently falls back to a generic default scale that has nothing to do with this rig's
       real ~8W ceiling. Setting this nonzero makes the watts<->fraction conversion linear
       against this ceiling instead, bypassing Hamlib's table lookup entirely. 0 = trust
       Hamlib (correct for a rig whose backend does populate a real power table). */
    float max_tx_power_watts;
} hal_cat_config_t;

hal_rc_t hal_cat_open(hal_cat_t** out, const hal_cat_config_t* config);
void     hal_cat_close(hal_cat_t* cat);

hal_rc_t hal_cat_set_freq_hz(hal_cat_t* cat, uint64_t hz);
hal_rc_t hal_cat_get_freq_hz(hal_cat_t* cat, uint64_t* out_hz);

hal_rc_t hal_cat_set_mode(hal_cat_t* cat, hal_cat_mode_t mode);
hal_rc_t hal_cat_get_mode(hal_cat_t* cat, hal_cat_mode_t* out_mode);

/* On/off, not a dB value -- the X6200 (like most rigs Hamlib exposes RIG_LEVEL_PREAMP for)
   has a single preamp stage. hal_cat_set_preamp(true) uses the rig's own first advertised
   preamp gain (rig->state.preamp[0], populated by Hamlib from the backend's rig_caps at
   rig_open()); (false) sets 0 dB (off). */
hal_rc_t hal_cat_set_preamp(hal_cat_t* cat, bool enable);
hal_rc_t hal_cat_get_preamp(hal_cat_t* cat, bool* out_enabled);

hal_rc_t hal_cat_set_agc(hal_cat_t* cat, hal_cat_agc_t agc);
hal_rc_t hal_cat_get_agc(hal_cat_t* cat, hal_cat_agc_t* out_agc);

/* Watts, not RIG_LEVEL_RFPOWER's raw [0.0..1.0] fraction -- converted via
   rig_mW2power()/rig_power2mW() against the rig's current frequency/mode, per Hamlib's own
   documented pattern for that level, unless hal_cat_config_t.max_tx_power_watts overrides
   it (see that field's comment). */
hal_rc_t hal_cat_set_power_watts(hal_cat_t* cat, float watts);
hal_rc_t hal_cat_get_power_watts(hal_cat_t* cat, float* out_watts);

/* atropos.c's watchdog is the one caller that must be able to reach this through
   sym_host_t.ptt_set without going through Hamlib's normal request path -- see
   core/sym_types.h. This function is what app/ wires that callback to. */
hal_rc_t hal_cat_set_ptt(hal_cat_t* cat, bool assert_tx);
hal_rc_t hal_cat_get_ptt(hal_cat_t* cat, bool* out_asserted);

const char* hal_cat_last_error(hal_cat_t* cat);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CAT_H */

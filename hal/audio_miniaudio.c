#include "hal_audio.h"
#include <stddef.h>

/* Phase 0 stub -- proves the seam links on both platforms. miniaudio.h isn't included yet;
   the real WASAPI/ALSA-backed implementation lands in Phase 1 (capture) and Phase 2
   (playback), per the kickoff's phasing table. */

struct hal_audio {
    int unused;
};

hal_rc_t hal_audio_enumerate(bool playback, hal_audio_device_t* out, size_t max_count,
                             size_t* out_count)
{
    (void)playback;
    (void)out;
    (void)max_count;
    if (out_count != NULL) {
        *out_count = 0;
    }
    return HAL_RC_UNSUPPORTED;
}

hal_rc_t hal_audio_open(hal_audio_t** out, const hal_audio_config_t* config,
                        hal_audio_capture_cb on_capture, hal_audio_playback_cb on_playback,
                        void* user)
{
    (void)config;
    (void)on_capture;
    (void)on_playback;
    (void)user;
    if (out != NULL) {
        *out = NULL;
    }
    return HAL_RC_UNSUPPORTED;
}

hal_rc_t hal_audio_start(hal_audio_t* audio)
{
    (void)audio;
    return HAL_RC_UNSUPPORTED;
}

hal_rc_t hal_audio_stop(hal_audio_t* audio)
{
    (void)audio;
    return HAL_RC_UNSUPPORTED;
}

void hal_audio_close(hal_audio_t* audio)
{
    (void)audio;
}

const char* hal_audio_last_error(hal_audio_t* audio)
{
    (void)audio;
    return "hal_audio: not yet implemented (Phase 0 stub)";
}

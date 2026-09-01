#ifndef HAL_AUDIO_H
#define HAL_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Implemented by hal/audio_miniaudio.c (Windows + Linux, via miniaudio -- one API over
   WASAPI + ALSA) or hal/audio_x6200.c (radio-native, ALSA direct; unimplemented stub for
   now -- see the kickoff's "genuine unknown" note on cat_x6200.c, same reasoning applies
   here: on-radio audio routing hasn't been characterized yet).

   Phase 0 ships this header plus stub .c files that link but return HAL_RC_UNSUPPORTED from
   every entry point. Real bodies land in Phase 1 (capture) and Phase 2 (playback/TX). */

typedef struct hal_audio hal_audio_t;

typedef struct {
    const char* capture_device;  /* NULL = system default */
    const char* playback_device; /* NULL = system default */
    uint32_t    sample_rate_hz;  /* 12000 for FT8 (see kickoff's FT8 protocol reference) */
    uint32_t    period_frames;   /* 0 = let the backend choose */
} hal_audio_config_t;

typedef struct {
    char id[256];
    char name[256];
    bool is_default;
} hal_audio_device_t;

/* Both callbacks run on the backend's realtime audio thread: no blocking, no heap
   allocation, no file/socket I/O. Mono float32 throughout -- capture hands samples straight
   to core's ring buffer (ring.c); playback pulls TX audio synthesized by tx.c. */
typedef void     (*hal_audio_capture_cb)(const float* in, uint32_t frame_count, void* user);
typedef uint32_t (*hal_audio_playback_cb)(float* out, uint32_t frame_count, void* user);

hal_rc_t hal_audio_enumerate(bool playback, hal_audio_device_t* out, size_t max_count,
                             size_t* out_count);

hal_rc_t hal_audio_open(hal_audio_t** out, const hal_audio_config_t* config,
                        hal_audio_capture_cb on_capture, hal_audio_playback_cb on_playback,
                        void* user);
hal_rc_t hal_audio_start(hal_audio_t* audio);
hal_rc_t hal_audio_stop(hal_audio_t* audio);
void     hal_audio_close(hal_audio_t* audio);

const char* hal_audio_last_error(hal_audio_t* audio);

#ifdef __cplusplus
}
#endif

#endif /* HAL_AUDIO_H */

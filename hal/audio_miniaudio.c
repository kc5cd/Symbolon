#include "hal_audio.h"

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <stdlib.h>
#include <string.h>

struct hal_audio {
    ma_context context;
    ma_device device;
    bool context_initialized;
    bool device_initialized;
    hal_audio_capture_cb on_capture;
    hal_audio_playback_cb on_playback;
    void* user;
    char last_error[256];
};

static void hal_audio_data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count)
{
    struct hal_audio* audio = (struct hal_audio*)device->pUserData;
    if (input != NULL && audio->on_capture != NULL) {
        audio->on_capture((const float*)input, frame_count, audio->user);
    }
    if (output != NULL && audio->on_playback != NULL) {
        audio->on_playback((float*)output, frame_count, audio->user);
    }
}

hal_rc_t hal_audio_enumerate(bool playback, hal_audio_device_t* out, size_t max_count, size_t* out_count)
{
    if (out_count != NULL) {
        *out_count = 0;
    }

    ma_context context;
    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
        return HAL_RC_ERROR;
    }

    ma_device_info* playback_infos;
    ma_uint32 playback_count;
    ma_device_info* capture_infos;
    ma_uint32 capture_count;
    if (ma_context_get_devices(&context, &playback_infos, &playback_count, &capture_infos, &capture_count) != MA_SUCCESS) {
        ma_context_uninit(&context);
        return HAL_RC_ERROR;
    }

    ma_device_info* infos = playback ? playback_infos : capture_infos;
    ma_uint32 count = playback ? playback_count : capture_count;

    size_t written = 0;
    for (ma_uint32 i = 0; i < count && written < max_count; ++i) {
        hal_audio_device_t* d = &out[written];
        strncpy(d->name, infos[i].name, sizeof(d->name) - 1);
        d->name[sizeof(d->name) - 1] = '\0';
        /* No stable cross-session id encoding yet -- matched by name in hal_audio_open()
           below. Fine for Phase 1 (single default-device session); revisit if a real
           persistent device id is ever needed (e.g. a config file pinning a specific
           interface). */
        strncpy(d->id, infos[i].name, sizeof(d->id) - 1);
        d->id[sizeof(d->id) - 1] = '\0';
        d->is_default = infos[i].isDefault != 0;
        ++written;
    }

    if (out_count != NULL) {
        *out_count = written;
    }

    ma_context_uninit(&context);
    return HAL_RC_OK;
}

hal_rc_t hal_audio_open(hal_audio_t** out, const hal_audio_config_t* config,
                        hal_audio_capture_cb on_capture, hal_audio_playback_cb on_playback,
                        void* user)
{
    if (out == NULL || config == NULL) {
        return HAL_RC_INVALID_ARG;
    }
    *out = NULL;

    struct hal_audio* audio = (struct hal_audio*)calloc(1, sizeof(struct hal_audio));
    if (audio == NULL) {
        return HAL_RC_ERROR;
    }
    audio->on_capture = on_capture;
    audio->on_playback = on_playback;
    audio->user = user;

    if (ma_context_init(NULL, 0, NULL, &audio->context) != MA_SUCCESS) {
        strncpy(audio->last_error, "ma_context_init failed", sizeof(audio->last_error) - 1);
        free(audio);
        return HAL_RC_ERROR;
    }
    audio->context_initialized = true;

    /* NULL capture_device/playback_device (the common case, per hal_audio_config_t's own
       doc) leaves pDeviceID NULL, which miniaudio resolves to the system default -- no
       enumeration needed on that path at all. */
    const ma_device_id* capture_id = NULL;
    ma_device_id resolved_capture_id;
    const ma_device_id* playback_id = NULL;
    ma_device_id resolved_playback_id;
    if (config->capture_device != NULL || config->playback_device != NULL) {
        ma_device_info* playback_infos;
        ma_uint32 playback_count;
        ma_device_info* capture_infos;
        ma_uint32 capture_count;
        if (ma_context_get_devices(&audio->context, &playback_infos, &playback_count, &capture_infos, &capture_count) == MA_SUCCESS) {
            if (config->capture_device != NULL) {
                for (ma_uint32 i = 0; i < capture_count; ++i) {
                    if (strcmp(capture_infos[i].name, config->capture_device) == 0) {
                        resolved_capture_id = capture_infos[i].id;
                        capture_id = &resolved_capture_id;
                        break;
                    }
                }
            }
            if (config->playback_device != NULL) {
                for (ma_uint32 i = 0; i < playback_count; ++i) {
                    if (strcmp(playback_infos[i].name, config->playback_device) == 0) {
                        resolved_playback_id = playback_infos[i].id;
                        playback_id = &resolved_playback_id;
                        break;
                    }
                }
            }
        }
    }

    ma_device_type device_type = (on_playback != NULL) ? ma_device_type_duplex : ma_device_type_capture;
    ma_device_config device_config = ma_device_config_init(device_type);
    device_config.capture.pDeviceID = capture_id;
    device_config.capture.format = ma_format_f32;
    device_config.capture.channels = 1;
    device_config.sampleRate = config->sample_rate_hz;
    device_config.dataCallback = hal_audio_data_callback;
    device_config.pUserData = audio;
    if (config->period_frames != 0) {
        device_config.periodSizeInFrames = config->period_frames;
    }
    if (on_playback != NULL) {
        device_config.playback.pDeviceID = playback_id;
        device_config.playback.format = ma_format_f32;
        device_config.playback.channels = 1;
    }

    if (ma_device_init(&audio->context, &device_config, &audio->device) != MA_SUCCESS) {
        strncpy(audio->last_error, "ma_device_init failed", sizeof(audio->last_error) - 1);
        ma_context_uninit(&audio->context);
        free(audio);
        return HAL_RC_ERROR;
    }
    audio->device_initialized = true;

    *out = audio;
    return HAL_RC_OK;
}

hal_rc_t hal_audio_start(hal_audio_t* audio)
{
    if (audio == NULL) {
        return HAL_RC_INVALID_ARG;
    }
    return (ma_device_start(&audio->device) == MA_SUCCESS) ? HAL_RC_OK : HAL_RC_ERROR;
}

hal_rc_t hal_audio_stop(hal_audio_t* audio)
{
    if (audio == NULL) {
        return HAL_RC_INVALID_ARG;
    }
    return (ma_device_stop(&audio->device) == MA_SUCCESS) ? HAL_RC_OK : HAL_RC_ERROR;
}

void hal_audio_close(hal_audio_t* audio)
{
    if (audio == NULL) {
        return;
    }
    if (audio->device_initialized) {
        ma_device_uninit(&audio->device);
    }
    if (audio->context_initialized) {
        ma_context_uninit(&audio->context);
    }
    free(audio);
}

const char* hal_audio_last_error(hal_audio_t* audio)
{
    if (audio == NULL) {
        return "hal_audio: null handle";
    }
    return (audio->last_error[0] != '\0') ? audio->last_error : "no error";
}

#include "tx.h"

#include <ft8/constants.h>
#include <ft8/encode.h>
#include <ft8/message.h>

#include <math.h>
#include <stdlib.h>

/* Not a standard C11 macro on MinGW-w64 without _USE_MATH_DEFINES -- same fix as
   core/argus.c's own ARGUS_M_PI, kept local rather than shared since these two files don't
   otherwise depend on each other. */
#ifndef SYM_TX_M_PI
#define SYM_TX_M_PI 3.14159265358979323846
#endif

/* GFSK synthesis (gfsk_pulse + the tone-shaping loop below) is ft8_lib's own algorithm,
   reimplemented fresh rather than linked -- it only exists in third_party/ft8_lib's
   demo/gen_ft8.c, which is an application (not part of the ft8/ or fft/ libraries core/
   already links) and, like common/monitor.c before it (see core/argus.c's own comment),
   isn't something core/ can pull in directly. Same math as the demo, first-party code; the
   one real change is heap-allocating the working buffers instead of the demo's stack VLAs
   (at 12 kHz/79 symbols that's ~600 KB of stack for a single call -- fine for a short-lived
   CLI tool, not something to bake into code that must also build for a future
   memory-constrained radio target). */

#define SYM_TX_SYMBOL_BT 2.0f    /* FT8's GFSK smoothing filter bandwidth factor */
#define SYM_TX_GFSK_K    5.336446f /* == pi * sqrt(2 / log(2)), gen_ft8.c's own GFSK_CONST_K */

static void gfsk_pulse(int n_spsym, float symbol_bt, float* pulse)
{
    for (int i = 0; i < 3 * n_spsym; ++i) {
        float t = i / (float)n_spsym - 1.5f;
        float arg1 = SYM_TX_GFSK_K * symbol_bt * (t + 0.5f);
        float arg2 = SYM_TX_GFSK_K * symbol_bt * (t - 0.5f);
        pulse[i] = (erff(arg1) - erff(arg2)) / 2.0f;
    }
}

static int synth_gfsk(const uint8_t* symbols, int n_sym, float f0, float symbol_bt,
    float symbol_period, int sample_rate_hz, float* signal)
{
    int n_spsym = (int)(0.5f + (float)sample_rate_hz * symbol_period);
    int n_wave = n_sym * n_spsym;

    float* dphi = (float*)malloc((size_t)(n_wave + 2 * n_spsym) * sizeof(float));
    float* pulse = (float*)malloc((size_t)(3 * n_spsym) * sizeof(float));
    if (dphi == NULL || pulse == NULL) {
        free(dphi);
        free(pulse);
        return -1;
    }

    float dphi_peak = 2.0f * (float)SYM_TX_M_PI / (float)n_spsym;
    for (int i = 0; i < n_wave + 2 * n_spsym; ++i) {
        dphi[i] = 2.0f * (float)SYM_TX_M_PI * f0 / (float)sample_rate_hz;
    }

    gfsk_pulse(n_spsym, symbol_bt, pulse);

    for (int i = 0; i < n_sym; ++i) {
        int ib = i * n_spsym;
        for (int j = 0; j < 3 * n_spsym; ++j) {
            dphi[j + ib] += dphi_peak * (float)symbols[i] * pulse[j];
        }
    }

    /* Dummy symbols at both ends, tone value equal to the first/last real symbol -- matches
       gen_ft8.c exactly; keeps the phase ramp continuous into/out of the burst. */
    for (int j = 0; j < 2 * n_spsym; ++j) {
        dphi[j] += dphi_peak * pulse[j + n_spsym] * (float)symbols[0];
        dphi[j + n_sym * n_spsym] += dphi_peak * pulse[j] * (float)symbols[n_sym - 1];
    }

    float phi = 0.0f;
    for (int k = 0; k < n_wave; ++k) {
        signal[k] = sinf(phi);
        phi = fmodf(phi + dphi[k + n_spsym], 2.0f * (float)SYM_TX_M_PI);
    }

    /* Raised-cosine envelope on the first/last few samples -- avoids a hard click at burst
       start/end. */
    int n_ramp = n_spsym / 8;
    for (int i = 0; i < n_ramp; ++i) {
        float env = (1.0f - cosf(2.0f * (float)SYM_TX_M_PI * (float)i / (2.0f * (float)n_ramp))) / 2.0f;
        signal[i] *= env;
        signal[n_wave - 1 - i] *= env;
    }

    free(dphi);
    free(pulse);
    return 0;
}

int sym_tx_signal_samples(int sample_rate_hz)
{
    int n_spsym = (int)(0.5f + (float)sample_rate_hz * FT8_SYMBOL_PERIOD);
    return FT8_NN * n_spsym;
}

sym_tx_rc_t sym_tx_synthesize(const char* text, float freq_hz, int sample_rate_hz, float* out_signal)
{
    ftx_message_t msg;
    if (ftx_message_encode(&msg, NULL, text) != FTX_MESSAGE_RC_OK) {
        return SYM_TX_ERROR_ENCODE;
    }

    uint8_t tones[FT8_NN];
    ft8_encode(msg.payload, tones);

    if (synth_gfsk(tones, FT8_NN, freq_hz, SYM_TX_SYMBOL_BT, FT8_SYMBOL_PERIOD, sample_rate_hz, out_signal) != 0) {
        return SYM_TX_ERROR_ENCODE;
    }
    return SYM_TX_OK;
}

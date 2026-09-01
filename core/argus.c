/* Argus -- see argus.h for the myth note. */
#include "argus.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef ARGUS_M_PI
#define ARGUS_M_PI 3.14159265358979323846
#endif

static float hann_window_sample(int i, int n)
{
    float x = sinf((float)ARGUS_M_PI * (float)i / (float)n);
    return x * x;
}

void argus_init(argus_t* argus, const argus_config_t* config)
{
    memset(argus, 0, sizeof(*argus));
    argus->config = *config;
    argus->symbol_period_s = FT8_SYMBOL_PERIOD;

    argus->block_size = (int)((float)config->sample_rate_hz * argus->symbol_period_s);
    argus->subblock_size = argus->block_size / config->time_osr;
    argus->nfft = argus->block_size * config->freq_osr;
    argus->fft_norm = 2.0f / (float)argus->nfft;

    argus->window = (float*)malloc((size_t)argus->nfft * sizeof(float));
    for (int i = 0; i < argus->nfft; ++i) {
        argus->window[i] = argus->fft_norm * hann_window_sample(i, argus->nfft);
    }
    argus->last_frame = (float*)calloc((size_t)argus->nfft, sizeof(float));

    size_t fft_work_size = 0;
    kiss_fftr_alloc(argus->nfft, 0, NULL, &fft_work_size);
    argus->fft_work = malloc(fft_work_size);
    argus->fft_cfg = kiss_fftr_alloc(argus->nfft, 0, argus->fft_work, &fft_work_size);

    argus->min_bin = (int)(config->f_min_hz * argus->symbol_period_s);
    int max_bin = (int)(config->f_max_hz * argus->symbol_period_s) + 1;
    int num_bins = max_bin - argus->min_bin;
    int max_blocks = (int)(FT8_SLOT_TIME / argus->symbol_period_s);

    argus->wf.max_blocks = max_blocks;
    argus->wf.num_blocks = 0;
    argus->wf.num_bins = num_bins;
    argus->wf.time_osr = config->time_osr;
    argus->wf.freq_osr = config->freq_osr;
    argus->wf.block_stride = config->time_osr * config->freq_osr * num_bins;
    argus->wf.protocol = FTX_PROTOCOL_FT8;
    argus->wf.mag = (WF_ELEM_T*)malloc((size_t)max_blocks * (size_t)argus->wf.block_stride * sizeof(WF_ELEM_T));
}

void argus_free(argus_t* argus)
{
    free(argus->window);
    free(argus->last_frame);
    free(argus->fft_work);
    free(argus->wf.mag);
    memset(argus, 0, sizeof(*argus));
}

void argus_reset(argus_t* argus)
{
    argus->wf.num_blocks = 0;
}

int argus_block_size(const argus_t* argus)
{
    return argus->block_size;
}

void argus_process_block(argus_t* argus, const float* samples)
{
    if (argus->wf.num_blocks >= argus->wf.max_blocks) {
        return;
    }

    int offset = argus->wf.num_blocks * argus->wf.block_stride;
    int frame_pos = 0;

    for (int time_sub = 0; time_sub < argus->wf.time_osr; ++time_sub) {
        kiss_fft_scalar timedata[argus->nfft];
        kiss_fft_cpx freqdata[argus->nfft / 2 + 1];

        for (int pos = 0; pos < argus->nfft - argus->subblock_size; ++pos) {
            argus->last_frame[pos] = argus->last_frame[pos + argus->subblock_size];
        }
        for (int pos = argus->nfft - argus->subblock_size; pos < argus->nfft; ++pos) {
            argus->last_frame[pos] = samples[frame_pos];
            ++frame_pos;
        }

        for (int pos = 0; pos < argus->nfft; ++pos) {
            timedata[pos] = argus->window[pos] * argus->last_frame[pos];
        }
        kiss_fftr(argus->fft_cfg, timedata, freqdata);

        for (int freq_sub = 0; freq_sub < argus->wf.freq_osr; ++freq_sub) {
            for (int bin = argus->min_bin; bin < argus->min_bin + argus->wf.num_bins; ++bin) {
                int src_bin = (bin * argus->wf.freq_osr) + freq_sub;
                float mag2 = (freqdata[src_bin].i * freqdata[src_bin].i) + (freqdata[src_bin].r * freqdata[src_bin].r);
                float db = 10.0f * log10f(1E-12f + mag2);

                int scaled = (int)(2 * db + 240);
                argus->wf.mag[offset] = (WF_ELEM_T)((scaled < 0) ? 0 : ((scaled > 255) ? 255 : scaled));
                ++offset;
            }
        }
    }

    ++argus->wf.num_blocks;
}

/* ftx_message_decode()'s hash_if callbacks (ft8/message.h) take no user-data pointer, so
   this table can't live inside argus_t -- it must be file-scope state, matching how
   ft8_lib's own demo/decode_ft8.c does it. Sized for one slot's worth of hashed-callsign
   references rather than a persistent multi-session table (Phase 1 scope: each WAV-corpus
   test case is a fresh process, and the live app doesn't need cross-slot memory yet either). */
#define ARGUS_HASH_TABLE_SIZE 64

static struct {
    char callsign[12];
    uint32_t hash;
} s_hash_table[ARGUS_HASH_TABLE_SIZE];

static bool argus_hash_lookup(ftx_callsign_hash_type_t hash_type, uint32_t hash, char* callsign)
{
    uint8_t hash_shift = (hash_type == FTX_CALLSIGN_HASH_10_BITS) ? 12 : (hash_type == FTX_CALLSIGN_HASH_12_BITS ? 10 : 0);
    uint16_t hash10 = (hash >> (12 - hash_shift)) & 0x3FFu;
    int idx = (hash10 * 23) % ARGUS_HASH_TABLE_SIZE;
    while (s_hash_table[idx].callsign[0] != '\0') {
        if ((s_hash_table[idx].hash >> hash_shift) == hash) {
            strcpy(callsign, s_hash_table[idx].callsign);
            return true;
        }
        idx = (idx + 1) % ARGUS_HASH_TABLE_SIZE;
    }
    callsign[0] = '\0';
    return false;
}

static void argus_hash_save(const char* callsign, uint32_t n22)
{
    uint16_t hash10 = (n22 >> 12) & 0x3FFu;
    int idx = (hash10 * 23) % ARGUS_HASH_TABLE_SIZE;
    while (s_hash_table[idx].callsign[0] != '\0') {
        if ((s_hash_table[idx].hash == n22) && (0 == strcmp(s_hash_table[idx].callsign, callsign))) {
            return; /* already present */
        }
        idx = (idx + 1) % ARGUS_HASH_TABLE_SIZE;
    }
    strncpy(s_hash_table[idx].callsign, callsign, 11);
    s_hash_table[idx].callsign[11] = '\0';
    s_hash_table[idx].hash = n22;
}

static ftx_callsign_hash_interface_t s_hash_if = {
    .lookup_hash = argus_hash_lookup,
    .save_hash = argus_hash_save,
};

int argus_decode_slot(argus_t* argus, argus_decode_t* out, int max_out)
{
    memset(s_hash_table, 0, sizeof(s_hash_table));

    const int kMaxCandidates = 140;
    const int kMinScore = 10;
    const int kLdpcIterations = 25;

    ftx_candidate_t candidates[140];
    int num_candidates = ftx_find_candidates(&argus->wf, kMaxCandidates, candidates, kMinScore);

    int num_out = 0;
    for (int i = 0; i < num_candidates && num_out < max_out; ++i) {
        const ftx_candidate_t* cand = &candidates[i];

        ftx_message_t message;
        ftx_decode_status_t status;
        if (!ftx_decode_candidate(&argus->wf, cand, kLdpcIterations, &message, &status)) {
            continue;
        }

        char text[FTX_MAX_MESSAGE_LENGTH];
        ftx_message_offsets_t offsets;
        if (ftx_message_decode(&message, &s_hash_if, text, &offsets) != FTX_MESSAGE_RC_OK) {
            continue;
        }

        argus_decode_t* d = &out[num_out];
        strncpy(d->text, text, FTX_MAX_MESSAGE_LENGTH - 1);
        d->text[FTX_MAX_MESSAGE_LENGTH - 1] = '\0';
        d->freq_hz = ((float)argus->min_bin + (float)cand->freq_offset + (float)cand->freq_sub / (float)argus->wf.freq_osr) / argus->symbol_period_s;
        d->time_s = ((float)cand->time_offset + (float)cand->time_sub / (float)argus->wf.time_osr) * argus->symbol_period_s;
        d->score = cand->score;
        ++num_out;
    }

    return num_out;
}

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

/* Inverse of argus_process_block()'s "scaled = 2*db + 240" waterfall quantization -- exact
   except for the +/-0.5 LSB (2 steps/dB) the quantization itself already threw away, which is
   negligible against the several-dB scale an SNR estimate is meaningful at. */
static float argus_bin_db(const argus_t* argus, int block, int time_sub, int freq_sub, int bin)
{
    int offset = block * argus->wf.block_stride
               + time_sub * (argus->wf.freq_osr * argus->wf.num_bins)
               + freq_sub * argus->wf.num_bins
               + bin;
    return ((float)argus->wf.mag[offset] - 240.0f) / 2.0f;
}

static int argus_float_compare(const void* a, const void* b)
{
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    return (fa > fb) - (fa < fb);
}

/* Slot-wide noise floor, in dB, as the median over a spread of (block, bin) samples at
   time_sub=freq_sub=0 -- median rather than mean so a strong nearby signal (or two) doesn't
   drag the estimate up. One measurement per argus_decode_slot() call, not per candidate: the
   whole monitored window (hundreds of Hz) is wide enough relative to one FT8 signal's ~50 Hz
   that a single slot-wide estimate is a fair background reference for every candidate in it. */
static float argus_noise_floor_db(const argus_t* argus)
{
    int num_blocks = argus->wf.num_blocks;
    int num_bins = argus->wf.num_bins;
    if (num_blocks <= 0 || num_bins <= 0) {
        return 0.0f;
    }

    size_t count = (size_t)num_blocks * (size_t)num_bins;
    float* samples = (float*)malloc(count * sizeof(float));
    if (samples == NULL) {
        return 0.0f;
    }

    size_t n = 0;
    for (int block = 0; block < num_blocks; ++block) {
        for (int bin = 0; bin < num_bins; ++bin) {
            samples[n++] = argus_bin_db(argus, block, 0, 0, bin);
        }
    }

    qsort(samples, n, sizeof(float), argus_float_compare);
    float median = samples[n / 2];
    free(samples);
    return median;
}

/* Signal power for one candidate, measured only at its own bin during the 21 Costas
   sync-symbol positions (FT8_NUM_SYNC groups of FT8_LENGTH_SYNC symbols, at the protocol's
   fixed FT8_SYNC_OFFSET spacing) -- the only symbol positions where the transmitted tone is
   known rather than one of 8 possible data tones, so this is an unambiguous power reading
   rather than a guess. Averages in linear power (not dB) across those 21 samples, then
   converts the average back to dB -- dB-domain averaging biases low. */
static float argus_estimate_snr_db(const argus_t* argus, const ftx_candidate_t* cand, float noise_floor_db)
{
    double power_sum = 0.0;
    int n = 0;

    for (int sync_group = 0; sync_group < FT8_NUM_SYNC; ++sync_group) {
        int base_block = cand->time_offset + sync_group * FT8_SYNC_OFFSET;
        for (int sym = 0; sym < FT8_LENGTH_SYNC; ++sym) {
            int block = base_block + sym;
            if (block < 0 || block >= argus->wf.num_blocks) {
                continue;
            }
            float db = argus_bin_db(argus, block, cand->time_sub, cand->freq_sub, cand->freq_offset);
            power_sum += pow(10.0, (double)db / 10.0);
            ++n;
        }
    }

    if (n == 0) {
        return 0.0f;
    }

    float signal_db = 10.0f * log10f((float)(power_sum / n));

    /* Both dB figures above are per-FFT-bin; normalize to the standard 2500 Hz reference
       noise bandwidth ham SNR figures are conventionally quoted in (matches WSJT-X's own
       reports), since white noise power scales with measurement bandwidth but the signal
       (one ~6.25 Hz-wide FSK tone) doesn't. */
    float bin_bw_hz = 1.0f / (argus->symbol_period_s * (float)argus->wf.freq_osr);
    float bw_correction_db = 10.0f * log10f(2500.0f / bin_bw_hz);

    /* ARGUS_SNR_CALIBRATION_DB absorbs everything the bandwidth-ratio term above doesn't
       model in closed form: the Hann window's noise-equivalent bandwidth (1.5x its bin
       spacing, not the bin spacing itself), the fft_norm scale factor argus_init() bakes into
       the window before the FFT, and kiss_fftr's own normalization convention -- none of
       which cancel out of a signal-minus-noise subtraction because the "signal" term is a
       narrowband coherent tone while the "noise" term is a wideband median, so the two don't
       carry the same effective-bandwidth correction. Deriving all of that by hand would mean
       re-deriving kiss_fftr's normalization from scratch; instead this single constant is
       fit empirically against known-power injected Gaussian noise (tests/core/test_argus_snr.c,
       which is also what re-validates it) -- standard practice for real SNR estimators, whose
       absolute calibration is always implementation-specific. Note the residual bias is not
       perfectly flat across SNR: tests/core/test_argus_snr.c's cases show it shrinking at
       very low SNR (the Costas-symbol "signal" measurement can't separate signal from noise
       power there, so it over-reads slightly), a well-known estimator bias that gets worse
       near/below the decode threshold -- expect several dB of residual error right where
       real over-the-air propagation measurements matter most, not just synthetic noise. */
    const float ARGUS_SNR_CALIBRATION_DB = 10.85f;

    return signal_db - noise_floor_db - bw_correction_db + ARGUS_SNR_CALIBRATION_DB;
}

int argus_decode_slot(argus_t* argus, argus_decode_t* out, int max_out)
{
    memset(s_hash_table, 0, sizeof(s_hash_table));

    const int kMaxCandidates = 140;
    const int kMinScore = 10;
    const int kLdpcIterations = 25;

    ftx_candidate_t candidates[140];
    int num_candidates = ftx_find_candidates(&argus->wf, kMaxCandidates, candidates, kMinScore);

    float noise_floor_db = argus_noise_floor_db(argus);

    /* ft8_lib's candidate search often finds the same underlying transmission at more than
       one adjacent frequency bin (bin width = 1/(symbol_period_s*freq_osr), ~3 Hz at this
       project's typical time_osr=2/freq_osr=2 config) -- each one independently decodes to
       the same message text. Collapse those into a single entry (keeping whichever candidate
       scored higher) instead of surfacing the same transmission twice per slot -- see issue
       #9. Matched on exact text equality within this frequency window; two distinct
       transmissions carrying identical text within one slot aren't expected in practice. */
    const float bin_bw_hz = 1.0f / (argus->symbol_period_s * (float)argus->wf.freq_osr);
    const float kDedupFreqWindowHz = 3.0f * bin_bw_hz;

    int num_out = 0;
    for (int i = 0; i < num_candidates; ++i) {
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

        argus_decode_t d;
        memset(&d, 0, sizeof(d));
        strncpy(d.text, text, FTX_MAX_MESSAGE_LENGTH - 1);
        d.text[FTX_MAX_MESSAGE_LENGTH - 1] = '\0';

        /* Dispatch on the message's real type first, matching what ftx_message_decode()
           itself does internally -- calling decode_std/decode_nonstd unconditionally would
           reinterpret a free-text (or other-typed) payload's raw bits as if they were a
           standard-message bit layout, which can spuriously "succeed" (RC_OK) with garbage
           fields (confirmed: an unresolved-hash "<...>" call_to came out of a genuine
           free-text message this way before this dispatch was added -- see
           tests/core/test_argus_snr.c's test_free_text_has_no_structured_fields). */
        ftx_field_t field_types[FTX_MAX_MESSAGE_FIELDS];
        ftx_message_type_t msg_type = ftx_message_get_type(&message);
        ftx_message_rc_t struct_rc = FTX_MESSAGE_RC_ERROR_TYPE;
        if (msg_type == FTX_MESSAGE_TYPE_STANDARD) {
            struct_rc = ftx_message_decode_std(&message, &s_hash_if, d.call_to, d.call_de, d.extra, field_types);
        } else if (msg_type == FTX_MESSAGE_TYPE_NONSTD_CALL) {
            struct_rc = ftx_message_decode_nonstd(&message, &s_hash_if, d.call_to, d.call_de, d.extra, field_types);
        }
        if (struct_rc != FTX_MESSAGE_RC_OK) {
            d.call_to[0] = '\0';
            d.call_de[0] = '\0';
            d.extra[0] = '\0';
        }

        d.freq_hz = ((float)argus->min_bin + (float)cand->freq_offset + (float)cand->freq_sub / (float)argus->wf.freq_osr) / argus->symbol_period_s;
        d.time_s = ((float)cand->time_offset + (float)cand->time_sub / (float)argus->wf.time_osr) * argus->symbol_period_s;
        d.score = cand->score;
        d.snr_db = argus_estimate_snr_db(argus, cand, noise_floor_db);

        int dup_index = -1;
        for (int j = 0; j < num_out; ++j) {
            if (fabsf(out[j].freq_hz - d.freq_hz) <= kDedupFreqWindowHz && strcmp(out[j].text, d.text) == 0) {
                dup_index = j;
                break;
            }
        }

        if (dup_index >= 0) {
            if (d.score > out[dup_index].score) {
                out[dup_index] = d;
            }
            continue;
        }

        if (num_out >= max_out) {
            continue;
        }

        out[num_out] = d;
        ++num_out;
    }

    return num_out;
}

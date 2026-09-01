#include "unity.h"
#include "argus.h"
#include "tx.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#ifndef TEST_M_PI
#define TEST_M_PI 3.14159265358979323846
#endif

/* ftx_message_decode_free() returns the full 13-char packed buffer, trailing space padding
   included (ftx_message_encode_free() pads short text out to exactly 13 chars before
   packing) -- not a bug, just something callers comparing against an un-padded original need
   to account for. Matches how ft8_lib has always behaved; Phase 1's corpus/live comparisons
   never hit this because the ground-truth text there was already in this padded shape. */
static bool text_matches_ignoring_trailing_spaces(const char* decoded, const char* expected)
{
    size_t len = strlen(decoded);
    while (len > 0 && decoded[len - 1] == ' ') {
        --len;
    }
    return (strlen(expected) == len) && (strncmp(decoded, expected, len) == 0);
}

/* Deterministic PRNG (not rand(), so the test is reproducible run-to-run and platform-to-
   platform) -- Gaussian noise via Box-Muller from a fixed-seed xorshift32 uniform source. */
static uint32_t s_rng_state;

static uint32_t xorshift32(void)
{
    uint32_t x = s_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng_state = x;
    return x;
}

static float uniform01(void)
{
    return (float)(xorshift32() >> 8) / (float)(1u << 24);
}

static float gaussian(void)
{
    float u1 = uniform01() + 1e-9f;
    float u2 = uniform01();
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float)TEST_M_PI * u2);
}

/* Synthesizes a message, adds Gaussian noise at a known power to hit a target true SNR (in
   the same 2500 Hz reference bandwidth argus_estimate_snr_db() reports in), decodes it, and
   asserts the reported snr_db lands within a few dB of the true injected value. This is
   Phase 3 prep, not part of its own test gate (cerberus.c/qso.c's table-driven tests are) --
   but argus_decode_t.snr_db is new, load-bearing input to cerberus.c's min-SNR predicate, so
   it needs its own correctness check before anything downstream trusts it. */
static void run_snr_case(float true_snr_db, float tolerance_db)
{
    s_rng_state = 0xC0FFEEu; /* fixed seed, same for every case -- reproducible */

    const int sample_rate_hz = 12000;
    const float tx_freq_hz = 1500.0f;
    const char* tx_text = "CQ KC5CD EM12";

    int tone_samples = sym_tx_signal_samples(sample_rate_hz);
    float* tone_buf = (float*)malloc((size_t)tone_samples * sizeof(float));
    TEST_ASSERT_NOT_NULL(tone_buf);
    TEST_ASSERT_EQUAL_INT(SYM_TX_OK, sym_tx_synthesize(tx_text, tx_freq_hz, sample_rate_hz, tone_buf));

    const int slot_samples = 15 * sample_rate_hz;
    const int offset_samples = 1 * sample_rate_hz;
    TEST_ASSERT_TRUE(offset_samples + tone_samples <= slot_samples);

    float* slot_buf = (float*)calloc((size_t)slot_samples, sizeof(float));
    TEST_ASSERT_NOT_NULL(slot_buf);
    memcpy(slot_buf + offset_samples, tone_buf, (size_t)tone_samples * sizeof(float));

    /* sym_tx_synthesize() writes a unit-ish amplitude tone; measure its actual mean power over
       the burst so the noise power we add is calibrated against the real signal, not assumed. */
    double signal_power = 0.0;
    for (int i = 0; i < tone_samples; ++i) {
        signal_power += (double)tone_buf[i] * (double)tone_buf[i];
    }
    signal_power /= tone_samples;

    /* true_snr_db here is defined in the same 2500 Hz reference bandwidth
       argus_estimate_snr_db() reports in, so the noise power added to the full-bandwidth time
       signal must be scaled up by (sample_rate_hz/2) / 2500 first -- white noise sampled at
       sample_rate_hz carries its power across the whole 0..sample_rate_hz/2 band, not just a
       2500 Hz slice of it. */
    double ref_bw_hz = 2500.0;
    double full_bw_hz = sample_rate_hz / 2.0;
    double noise_power_ref_bw = signal_power / pow(10.0, true_snr_db / 10.0);
    double noise_power_full_bw = noise_power_ref_bw * (full_bw_hz / ref_bw_hz);
    float noise_amplitude = (float)sqrt(noise_power_full_bw);

    for (int i = 0; i < slot_samples; ++i) {
        slot_buf[i] += noise_amplitude * gaussian();
    }

    argus_config_t cfg = { 0 };
    cfg.f_min_hz = 200.0f;
    cfg.f_max_hz = 3000.0f;
    cfg.sample_rate_hz = sample_rate_hz;
    cfg.time_osr = 2;
    cfg.freq_osr = 2;

    argus_t argus;
    argus_init(&argus, &cfg);

    int block_size = argus_block_size(&argus);
    for (int pos = 0; pos + block_size <= slot_samples; pos += block_size) {
        argus_process_block(&argus, slot_buf + pos);
    }

    argus_decode_t decodes[16];
    int num_decoded = argus_decode_slot(&argus, decodes, 16);
    argus_free(&argus);

    bool found = false;
    float reported_snr_db = 0.0f;
    for (int i = 0; i < num_decoded; ++i) {
        if (strcmp(decodes[i].text, tx_text) == 0) {
            found = true;
            reported_snr_db = decodes[i].snr_db;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "message not recovered at this noise level");

    TEST_ASSERT_FLOAT_WITHIN(tolerance_db, true_snr_db, reported_snr_db);

    free(tone_buf);
    free(slot_buf);
}

static void test_snr_at_minus_10db(void)
{
    run_snr_case(-10.0f, 4.0f);
}

static void test_snr_at_0db(void)
{
    run_snr_case(0.0f, 4.0f);
}

static void test_snr_at_plus_10db(void)
{
    run_snr_case(10.0f, 4.0f);
}

/* No structured call_to/call_de decode should ever surface for a message the tx side packed
   as free text ("CQ KC5CD EM12" -- 3 tokens, decodes_std as call_to=CQ/call_de=KC5CD/extra=
   EM12 actually IS standard-shaped, see below); this case instead checks the empty-field
   fallback shape on a genuine free-text-only message. */
static void test_free_text_has_no_structured_fields(void)
{
    s_rng_state = 0xC0FFEEu;

    const int sample_rate_hz = 12000;
    /* 4 space-separated tokens forces ftx_message_encode()'s free-text fallback: it only
       attempts encode_std/encode_nonstd for <=3 tokens (see message.c's parse_position
       check). A 3-token all-caps message like "TNX FER TEST" looked like a natural free-text
       choice but isn't one -- ft8_lib packs short tokens as *hashed* callsign references
       there instead of failing outright, so it round-trips as an unresolved "<...> <...>"
       nonstandard-call decode (this decoder's hash table is only populated by prior sightings
       in the same session, and a synthetic single-shot test never primes it). */
    const char* tx_text = "A B C D";

    int tone_samples = sym_tx_signal_samples(sample_rate_hz);
    float* tone_buf = (float*)malloc((size_t)tone_samples * sizeof(float));
    TEST_ASSERT_NOT_NULL(tone_buf);
    TEST_ASSERT_EQUAL_INT(SYM_TX_OK, sym_tx_synthesize(tx_text, 1500.0f, sample_rate_hz, tone_buf));

    const int slot_samples = 15 * sample_rate_hz;
    const int offset_samples = 1 * sample_rate_hz;
    float* slot_buf = (float*)calloc((size_t)slot_samples, sizeof(float));
    TEST_ASSERT_NOT_NULL(slot_buf);
    memcpy(slot_buf + offset_samples, tone_buf, (size_t)tone_samples * sizeof(float));

    argus_config_t cfg = { 0 };
    cfg.f_min_hz = 200.0f;
    cfg.f_max_hz = 3000.0f;
    cfg.sample_rate_hz = sample_rate_hz;
    cfg.time_osr = 2;
    cfg.freq_osr = 2;

    argus_t argus;
    argus_init(&argus, &cfg);
    int block_size = argus_block_size(&argus);
    for (int pos = 0; pos + block_size <= slot_samples; pos += block_size) {
        argus_process_block(&argus, slot_buf + pos);
    }

    argus_decode_t decodes[16];
    int num_decoded = argus_decode_slot(&argus, decodes, 16);
    argus_free(&argus);

    bool found = false;
    for (int i = 0; i < num_decoded; ++i) {
        if (text_matches_ignoring_trailing_spaces(decodes[i].text, tx_text)) {
            TEST_ASSERT_EQUAL_STRING("", decodes[i].call_to);
            TEST_ASSERT_EQUAL_STRING("", decodes[i].call_de);
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "free-text message not recovered");

    free(tone_buf);
    free(slot_buf);
}

static void test_standard_message_has_structured_fields(void)
{
    s_rng_state = 0xC0FFEEu;

    const int sample_rate_hz = 12000;
    const char* tx_text = "CQ KC5CD EM12";

    int tone_samples = sym_tx_signal_samples(sample_rate_hz);
    float* tone_buf = (float*)malloc((size_t)tone_samples * sizeof(float));
    TEST_ASSERT_NOT_NULL(tone_buf);
    TEST_ASSERT_EQUAL_INT(SYM_TX_OK, sym_tx_synthesize(tx_text, 1500.0f, sample_rate_hz, tone_buf));

    const int slot_samples = 15 * sample_rate_hz;
    const int offset_samples = 1 * sample_rate_hz;
    float* slot_buf = (float*)calloc((size_t)slot_samples, sizeof(float));
    TEST_ASSERT_NOT_NULL(slot_buf);
    memcpy(slot_buf + offset_samples, tone_buf, (size_t)tone_samples * sizeof(float));

    argus_config_t cfg = { 0 };
    cfg.f_min_hz = 200.0f;
    cfg.f_max_hz = 3000.0f;
    cfg.sample_rate_hz = sample_rate_hz;
    cfg.time_osr = 2;
    cfg.freq_osr = 2;

    argus_t argus;
    argus_init(&argus, &cfg);
    int block_size = argus_block_size(&argus);
    for (int pos = 0; pos + block_size <= slot_samples; pos += block_size) {
        argus_process_block(&argus, slot_buf + pos);
    }

    argus_decode_t decodes[16];
    int num_decoded = argus_decode_slot(&argus, decodes, 16);
    argus_free(&argus);

    bool found = false;
    for (int i = 0; i < num_decoded; ++i) {
        if (strcmp(decodes[i].text, tx_text) == 0) {
            TEST_ASSERT_EQUAL_STRING("CQ", decodes[i].call_to);
            TEST_ASSERT_EQUAL_STRING("KC5CD", decodes[i].call_de);
            TEST_ASSERT_EQUAL_STRING("EM12", decodes[i].extra);
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "standard message not recovered");

    free(tone_buf);
    free(slot_buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_snr_at_minus_10db);
    RUN_TEST(test_snr_at_0db);
    RUN_TEST(test_snr_at_plus_10db);
    RUN_TEST(test_free_text_has_no_structured_fields);
    RUN_TEST(test_standard_message_has_structured_fields);
    return UNITY_END();
}

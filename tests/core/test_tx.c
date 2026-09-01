#include "unity.h"
#include "argus.h"
#include "tx.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Phase 2's own test gate (see the kickoff's phasing table): an in-process encode->decode
   round-trip. Synthesizes a message with core/tx.c, feeds the resulting audio straight back
   through core/argus.c (Phase 1's decoder), and confirms the same text comes back out --
   proves the TX chain is self-consistent before it's ever wired to a radio. */
static void test_encode_decode_roundtrip(void)
{
    const int sample_rate_hz = 12000;
    const float tx_freq_hz = 1500.0f;
    const char* tx_text = "CQ KC5CD EM12";

    int tone_samples = sym_tx_signal_samples(sample_rate_hz);
    float* tone_buf = (float*)malloc((size_t)tone_samples * sizeof(float));
    TEST_ASSERT_NOT_NULL(tone_buf);
    TEST_ASSERT_EQUAL_INT(SYM_TX_OK, sym_tx_synthesize(tx_text, tx_freq_hz, sample_rate_hz, tone_buf));

    /* Place the burst inside a zeroed 15 s slot buffer, offset by ~1 s -- argus_decode_slot()
       reports a DT (time offset) alongside every real decode, so a signal sitting at t=0
       with no lead-in silence isn't representative of what it'll actually see on the air. */
    const int slot_samples = 15 * sample_rate_hz;
    const int offset_samples = 1 * sample_rate_hz;
    TEST_ASSERT_TRUE(offset_samples + tone_samples <= slot_samples);

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
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "synthesized message was not recovered by argus_decode_slot");

    free(tone_buf);
    free(slot_buf);
}

static void test_encode_rejects_invalid_text(void)
{
    /* Free text over 13 characters with no valid standard-message parse (this one has way
       too many space-separated tokens for encode_std, and is too long for encode_free) --
       ftx_message_encode() should fail cleanly, not synthesize garbage. */
    float dummy[1];
    sym_tx_rc_t rc = sym_tx_synthesize(
        "this is definitely not a valid ft8 message at all", 1500.0f, 12000, dummy);
    TEST_ASSERT_EQUAL_INT(SYM_TX_ERROR_ENCODE, rc);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_decode_roundtrip);
    RUN_TEST(test_encode_rejects_invalid_text);
    return UNITY_END();
}

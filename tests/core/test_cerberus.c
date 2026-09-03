#include "unity.h"
#include "cerberus.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Phase 3's own test gate (kickoff's phasing table): "table-driven cerberus.c match tests".
   Builds argus_decode_t fixtures directly rather than synthesizing/decoding real audio --
   cerberus.c only ever sees a populated argus_decode_t, never argus.c internals, so this is
   the right boundary to test at (argus.c's own decode correctness is core.argus_snr's job). */

static argus_decode_t make_decode(const char* text, const char* call_to, const char* call_de,
                                  const char* extra, float freq_hz, float snr_db)
{
    argus_decode_t d;
    memset(&d, 0, sizeof(d));
    strncpy(d.text, text, sizeof(d.text) - 1);
    strncpy(d.call_to, call_to, sizeof(d.call_to) - 1);
    strncpy(d.call_de, call_de, sizeof(d.call_de) - 1);
    strncpy(d.extra, extra, sizeof(d.extra) - 1);
    d.freq_hz = freq_hz;
    d.snr_db = snr_db;
    d.score = 20;
    return d;
}

static cerberus_config_t make_config(void)
{
    cerberus_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.my_call, "KC5CD", sizeof(cfg.my_call) - 1);
    strncpy(cfg.whitelist[0], "W5XYZ", sizeof(cfg.whitelist[0]) - 1);
    cfg.whitelist_count = 1;
    return cfg;
}

/* --- Standard exchange path (structured call_to/call_de) --- */

static void test_standard_exchange_matches(void)
{
    cerberus_config_t cfg = make_config();
    /* His report of me -- call_to=KC5CD (directed at me), call_de=W5XYZ (the whitelisted
       friend), matching the kickoff's own exchange sequence ("him: KC5CD W5XYZ -12"). */
    argus_decode_t d = make_decode("KC5CD W5XYZ -12", "KC5CD", "W5XYZ", "-12", 1500.0f, 10.0f);

    cerberus_result_t r = cerberus_evaluate(&cfg, &d, NULL);

    TEST_ASSERT_TRUE(r.matched);
    TEST_ASSERT_TRUE(r.whitelist_ok);
    TEST_ASSERT_TRUE(r.directed_at_me_ok);
    TEST_ASSERT_TRUE(r.text_ok);
    TEST_ASSERT_TRUE(r.gates_ok);
    TEST_ASSERT_FALSE(r.is_beacon_token);
}

static void test_cq_is_not_directed_at_me(void)
{
    /* CQs and third-party traffic must be ignored per the kickoff -- call_to is "CQ", not
       my_call, even though call_de is on the whitelist. */
    cerberus_config_t cfg = make_config();
    argus_decode_t d = make_decode("CQ W5XYZ EM12", "CQ", "W5XYZ", "EM12", 1500.0f, 10.0f);

    cerberus_result_t r = cerberus_evaluate(&cfg, &d, NULL);

    TEST_ASSERT_FALSE(r.matched);
    TEST_ASSERT_TRUE(r.whitelist_ok);
    TEST_ASSERT_FALSE(r.directed_at_me_ok);
}

static void test_non_whitelisted_caller_does_not_match(void)
{
    /* Directed at me, but call_de isn't on the whitelist -- e.g. a stranger replying to my
       own CQ. Must not match even though it's aimed straight at me. */
    cerberus_config_t cfg = make_config();
    argus_decode_t d = make_decode("KC5CD N0CALL -05", "KC5CD", "N0CALL", "-05", 1500.0f, 10.0f);

    cerberus_result_t r = cerberus_evaluate(&cfg, &d, NULL);

    TEST_ASSERT_FALSE(r.matched);
    TEST_ASSERT_FALSE(r.whitelist_ok);
    TEST_ASSERT_TRUE(r.directed_at_me_ok);
}

static void test_unresolved_hashed_call_does_not_match(void)
{
    /* An unresolved hash placeholder ("<...>", see argus.c) must never be treated as a real
       callsign -- it should fail both whitelist and directed-at-me, not accidentally pass
       because it's non-empty. */
    cerberus_config_t cfg = make_config();
    argus_decode_t d = make_decode("<...> <...> RR73", "<...>", "<...>", "RR73", 1500.0f, 10.0f);

    cerberus_result_t r = cerberus_evaluate(&cfg, &d, NULL);

    TEST_ASSERT_FALSE(r.matched);
    TEST_ASSERT_FALSE(r.whitelist_ok);
    TEST_ASSERT_FALSE(r.directed_at_me_ok);
}

static void test_free_text_with_no_beacon_token_configured_does_not_match(void)
{
    /* Genuine free text, no structured fields, and no beacon token configured at all -- must
       not match, and text_ok should honestly reflect that nothing recognizable was found. */
    cerberus_config_t cfg = make_config();
    argus_decode_t d = make_decode("GM ALL GREAT DAY", "", "", "", 1500.0f, 10.0f);

    cerberus_result_t r = cerberus_evaluate(&cfg, &d, NULL);

    TEST_ASSERT_FALSE(r.matched);
    TEST_ASSERT_FALSE(r.text_ok);
    TEST_ASSERT_FALSE(r.is_beacon_token);
}

/* --- Gates --- */

static void test_snr_gate_rejects_below_threshold(void)
{
    cerberus_config_t cfg = make_config();
    cfg.has_snr_gate = true;
    cfg.min_snr_db = 0.0f;
    argus_decode_t d = make_decode("KC5CD W5XYZ -12", "KC5CD", "W5XYZ", "-12", 1500.0f, -5.0f);

    cerberus_result_t r = cerberus_evaluate(&cfg, &d, NULL);

    TEST_ASSERT_FALSE(r.matched);
    TEST_ASSERT_FALSE(r.gates_ok);
}

static void test_snr_gate_accepts_above_threshold(void)
{
    cerberus_config_t cfg = make_config();
    cfg.has_snr_gate = true;
    cfg.min_snr_db = 0.0f;
    argus_decode_t d = make_decode("KC5CD W5XYZ -12", "KC5CD", "W5XYZ", "-12", 1500.0f, 5.0f);

    cerberus_result_t r = cerberus_evaluate(&cfg, &d, NULL);

    TEST_ASSERT_TRUE(r.matched);
    TEST_ASSERT_TRUE(r.gates_ok);
}

static void test_freq_gate_rejects_outside_window(void)
{
    cerberus_config_t cfg = make_config();
    cfg.has_freq_gate = true;
    cfg.freq_min_hz = 1000.0f;
    cfg.freq_max_hz = 2000.0f;
    argus_decode_t d = make_decode("KC5CD W5XYZ -12", "KC5CD", "W5XYZ", "-12", 2500.0f, 10.0f);

    cerberus_result_t r = cerberus_evaluate(&cfg, &d, NULL);

    TEST_ASSERT_FALSE(r.matched);
    TEST_ASSERT_FALSE(r.gates_ok);
}

static void test_band_gate_rejects_wrong_band(void)
{
    cerberus_config_t cfg = make_config();
    cfg.has_band_gate = true;
    strncpy(cfg.band, "20m", sizeof(cfg.band) - 1);
    argus_decode_t d = make_decode("KC5CD W5XYZ -12", "KC5CD", "W5XYZ", "-12", 1500.0f, 10.0f);

    cerberus_result_t r = cerberus_evaluate(&cfg, &d, "40m");

    TEST_ASSERT_FALSE(r.matched);
    TEST_ASSERT_FALSE(r.gates_ok);
}

static void test_band_gate_accepts_matching_band(void)
{
    cerberus_config_t cfg = make_config();
    cfg.has_band_gate = true;
    strncpy(cfg.band, "20m", sizeof(cfg.band) - 1);
    argus_decode_t d = make_decode("KC5CD W5XYZ -12", "KC5CD", "W5XYZ", "-12", 1500.0f, 10.0f);

    cerberus_result_t r = cerberus_evaluate(&cfg, &d, "20m");

    TEST_ASSERT_TRUE(r.matched);
}

/* --- Beacon token path --- */

static void test_beacon_token_requires_callsigns_by_default(void)
{
    /* Default (beacon_allow_token_alone == false): station identification is still required
       on the beacon path, matching Part 97's identify-every-transmission requirement. */
    cerberus_config_t cfg = make_config();
    strncpy(cfg.beacon_token, "PT1", sizeof(cfg.beacon_token) - 1);

    /* Token present, but neither call appears anywhere in the free text. */
    argus_decode_t d = make_decode("PT1", "", "", "", 1500.0f, 10.0f);
    cerberus_result_t r = cerberus_evaluate(&cfg, &d, NULL);
    TEST_ASSERT_FALSE(r.matched);
    TEST_ASSERT_TRUE(r.is_beacon_token);
    TEST_ASSERT_FALSE(r.whitelist_ok);
    TEST_ASSERT_FALSE(r.directed_at_me_ok);

    /* Token present, both calls present as whole words -- default mode satisfied. */
    argus_decode_t d2 = make_decode("KC5CD W5XYZ PT1", "", "", "", 1500.0f, 10.0f);
    cerberus_result_t r2 = cerberus_evaluate(&cfg, &d2, NULL);
    TEST_ASSERT_TRUE(r2.matched);
    TEST_ASSERT_TRUE(r2.whitelist_ok);
    TEST_ASSERT_TRUE(r2.directed_at_me_ok);
}

static void test_beacon_token_alone_authenticates_with_escape_hatch(void)
{
    /* beacon_allow_token_alone is the deliberately-undocumented escape hatch -- see
       cerberus.h's own note on why it stays off any --help/README surface. */
    cerberus_config_t cfg = make_config();
    strncpy(cfg.beacon_token, "PROPTEST1", sizeof(cfg.beacon_token) - 1);
    cfg.beacon_allow_token_alone = true;

    /* No call_to/call_de at all -- genuine free text, as ft8_lib actually decodes it. */
    argus_decode_t d = make_decode("PROPTEST1", "", "", "", 1500.0f, 10.0f);

    cerberus_result_t r = cerberus_evaluate(&cfg, &d, NULL);

    TEST_ASSERT_TRUE(r.matched);
    TEST_ASSERT_TRUE(r.is_beacon_token);
    TEST_ASSERT_TRUE(r.whitelist_ok);
    TEST_ASSERT_TRUE(r.directed_at_me_ok);
}

static void test_beacon_token_still_gated_by_snr(void)
{
    /* The beacon token (either mode) bypasses nothing about the propagation gates -- SNR/
       band/freq gates are a separate AND'd category regardless of which text path matched. */
    cerberus_config_t cfg = make_config();
    strncpy(cfg.beacon_token, "PT1", sizeof(cfg.beacon_token) - 1);
    cfg.has_snr_gate = true;
    cfg.min_snr_db = 0.0f;
    argus_decode_t d = make_decode("KC5CD W5XYZ PT1", "", "", "", 1500.0f, -8.0f);

    cerberus_result_t r = cerberus_evaluate(&cfg, &d, NULL);

    TEST_ASSERT_FALSE(r.matched);
    TEST_ASSERT_TRUE(r.is_beacon_token);
    TEST_ASSERT_FALSE(r.gates_ok);
}

static void test_token_substring_of_another_word_does_not_count(void)
{
    /* text_contains_token() must match whole words only -- a token that happens to be a
       substring of some other word in the message must not spuriously trigger the beacon
       path. */
    cerberus_config_t cfg = make_config();
    strncpy(cfg.beacon_token, "TEST", sizeof(cfg.beacon_token) - 1);

    argus_decode_t d = make_decode("KC5CD W5XYZ TESTING", "KC5CD", "W5XYZ", "TESTING", 1500.0f, 10.0f);
    cerberus_result_t r = cerberus_evaluate(&cfg, &d, NULL);

    TEST_ASSERT_FALSE(r.is_beacon_token);
    /* Still matches via the ordinary structured exchange path, though -- the false beacon hit
       just shouldn't be the reason. */
    TEST_ASSERT_TRUE(r.matched);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_standard_exchange_matches);
    RUN_TEST(test_cq_is_not_directed_at_me);
    RUN_TEST(test_non_whitelisted_caller_does_not_match);
    RUN_TEST(test_unresolved_hashed_call_does_not_match);
    RUN_TEST(test_free_text_with_no_beacon_token_configured_does_not_match);
    RUN_TEST(test_snr_gate_rejects_below_threshold);
    RUN_TEST(test_snr_gate_accepts_above_threshold);
    RUN_TEST(test_freq_gate_rejects_outside_window);
    RUN_TEST(test_band_gate_rejects_wrong_band);
    RUN_TEST(test_band_gate_accepts_matching_band);
    RUN_TEST(test_beacon_token_requires_callsigns_by_default);
    RUN_TEST(test_beacon_token_alone_authenticates_with_escape_hatch);
    RUN_TEST(test_beacon_token_still_gated_by_snr);
    RUN_TEST(test_token_substring_of_another_word_does_not_count);
    return UNITY_END();
}

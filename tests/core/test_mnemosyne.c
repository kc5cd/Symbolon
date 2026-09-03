#include "unity.h"
#include "mnemosyne.h"

#include <string.h>

/* Phase 5 prep, not a formal phasing-table gate (mnemosyne.c is planned-but-unbuilt per
   GitHub issue #10) -- exercised the same way atropos.c's watchdog is: a fake sink captures
   what core/ would otherwise hand off to a host callback, no file I/O or SQLite involved. */

static int s_observation_count;
static mnemosyne_observation_t s_last_observation;
static int s_qso_count;
static mnemosyne_qso_t s_last_qso;

static void fake_on_observation(void* user, const mnemosyne_observation_t* obs)
{
    (void)user;
    ++s_observation_count;
    s_last_observation = *obs;
}

static void fake_on_qso_complete(void* user, const mnemosyne_qso_t* qso)
{
    (void)user;
    ++s_qso_count;
    s_last_qso = *qso;
}

void setUp(void)
{
    s_observation_count = 0;
    memset(&s_last_observation, 0, sizeof(s_last_observation));
    s_qso_count = 0;
    memset(&s_last_qso, 0, sizeof(s_last_qso));
}

void tearDown(void) {}

static mnemosyne_sink_t make_fake_sink(void)
{
    mnemosyne_sink_t sink;
    sink.on_observation = fake_on_observation;
    sink.on_qso_complete = fake_on_qso_complete;
    sink.user = NULL;
    return sink;
}

static argus_decode_t make_decode(const char* text, const char* call_to, const char* call_de,
                                  float freq_hz, float snr_db)
{
    argus_decode_t d;
    memset(&d, 0, sizeof(d));
    strncpy(d.text, text, sizeof(d.text) - 1);
    strncpy(d.call_to, call_to, sizeof(d.call_to) - 1);
    strncpy(d.call_de, call_de, sizeof(d.call_de) - 1);
    d.freq_hz = freq_hz;
    d.snr_db = snr_db;
    return d;
}

static cerberus_result_t make_result(bool whitelist_ok, bool is_beacon_token)
{
    cerberus_result_t r;
    memset(&r, 0, sizeof(r));
    r.whitelist_ok = whitelist_ok;
    r.is_beacon_token = is_beacon_token;
    return r;
}

/* --- mnemosyne_observe() --- */

static void test_observe_logs_whitelisted_decode(void)
{
    mnemosyne_t m;
    mnemosyne_sink_t sink = make_fake_sink();
    mnemosyne_init(&m, &sink);

    argus_decode_t d = make_decode("W5XYZ KC5CD -12", "W5XYZ", "KC5CD", 1500.0f, -12.0f);
    cerberus_result_t r = make_result(true, false);

    mnemosyne_observe(&m, &d, &r, "20m", 1000000ULL);

    TEST_ASSERT_EQUAL_INT(1, s_observation_count);
    TEST_ASSERT_EQUAL_UINT64(1000000ULL, s_last_observation.utc_us);
    TEST_ASSERT_EQUAL_STRING("KC5CD", s_last_observation.call_de);
    TEST_ASSERT_EQUAL_STRING("W5XYZ", s_last_observation.call_to);
    TEST_ASSERT_EQUAL_STRING("20m", s_last_observation.band);
    TEST_ASSERT_EQUAL_STRING("W5XYZ KC5CD -12", s_last_observation.text);
    TEST_ASSERT_EQUAL_FLOAT(1500.0f, s_last_observation.freq_hz);
    TEST_ASSERT_EQUAL_FLOAT(-12.0f, s_last_observation.snr_db);
    TEST_ASSERT_FALSE(s_last_observation.is_beacon_token);
}

/* Heard-but-not-worked's whole point: a whitelisted station's own CQ (never directed at me,
   cerberus_evaluate() would never set matched=true for it) is still worth recording -- see
   mnemosyne.h's own note on why this checks whitelist_ok alone, not cerberus's `matched`. */
static void test_observe_logs_unmatched_cq_from_whitelisted_station(void)
{
    mnemosyne_t m;
    mnemosyne_sink_t sink = make_fake_sink();
    mnemosyne_init(&m, &sink);

    argus_decode_t cq = make_decode("CQ KC5CD EM10", "CQ", "KC5CD", 1500.0f, 5.0f);
    cerberus_result_t r = make_result(true, false);

    mnemosyne_observe(&m, &cq, &r, NULL, 0ULL);

    TEST_ASSERT_EQUAL_INT(1, s_observation_count);
}

static void test_observe_skips_non_whitelisted_decode(void)
{
    mnemosyne_t m;
    mnemosyne_sink_t sink = make_fake_sink();
    mnemosyne_init(&m, &sink);

    argus_decode_t d = make_decode("KC5CD N0CALL -05", "KC5CD", "N0CALL", 1500.0f, -5.0f);
    cerberus_result_t r = make_result(false, false);

    mnemosyne_observe(&m, &d, &r, NULL, 0ULL);

    TEST_ASSERT_EQUAL_INT(0, s_observation_count);
}

static void test_observe_dedups_within_a_slot(void)
{
    mnemosyne_t m;
    mnemosyne_sink_t sink = make_fake_sink();
    mnemosyne_init(&m, &sink);

    argus_decode_t d = make_decode("W5XYZ KC5CD -12", "W5XYZ", "KC5CD", 1500.0f, -12.0f);
    cerberus_result_t r = make_result(true, false);

    /* argus_decode_slot() can surface the same message twice via different candidates (see
       argus.h) -- the second occurrence in the same slot must not double-log. */
    mnemosyne_observe(&m, &d, &r, NULL, 0ULL);
    mnemosyne_observe(&m, &d, &r, NULL, 0ULL);
    TEST_ASSERT_EQUAL_INT(1, s_observation_count);

    /* A different message from the same station in the same slot is a distinct event. */
    argus_decode_t d2 = make_decode("W5XYZ KC5CD RR73", "W5XYZ", "KC5CD", 1500.0f, -12.0f);
    mnemosyne_observe(&m, &d2, &r, NULL, 0ULL);
    TEST_ASSERT_EQUAL_INT(2, s_observation_count);

    /* mnemosyne_slot_reset() clears the dedup set -- the next slot can log the same text again. */
    mnemosyne_slot_reset(&m);
    mnemosyne_observe(&m, &d, &r, NULL, 0ULL);
    TEST_ASSERT_EQUAL_INT(3, s_observation_count);
}

/* --- mnemosyne_log_qso() --- */

static void test_log_qso_emits_when_both_snr_directions_valid(void)
{
    mnemosyne_t m;
    mnemosyne_sink_t sink = make_fake_sink();
    mnemosyne_init(&m, &sink);

    qso_t qso;
    qso_init(&qso);
    strncpy(qso.peer_call, "W5XYZ", sizeof(qso.peer_call) - 1);
    qso.snr_i_sent = 7;
    qso.snr_i_sent_valid = true;
    qso.snr_i_got = -12;
    qso.snr_i_got_valid = true;

    qso_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.my_call, "KC5CD", sizeof(cfg.my_call) - 1);

    mnemosyne_log_qso(&m, &qso, &cfg, 42ULL);

    TEST_ASSERT_EQUAL_INT(1, s_qso_count);
    TEST_ASSERT_EQUAL_UINT64(42ULL, s_last_qso.utc_us);
    TEST_ASSERT_EQUAL_STRING("KC5CD", s_last_qso.my_call);
    TEST_ASSERT_EQUAL_STRING("W5XYZ", s_last_qso.peer_call);
    TEST_ASSERT_EQUAL_INT(7, s_last_qso.snr_i_sent);
    TEST_ASSERT_EQUAL_INT(-12, s_last_qso.snr_i_got);
    TEST_ASSERT_EQUAL_INT(-19, s_last_qso.asymmetry_db);
}

static void test_log_qso_is_noop_when_snr_incomplete(void)
{
    mnemosyne_t m;
    mnemosyne_sink_t sink = make_fake_sink();
    mnemosyne_init(&m, &sink);

    qso_t qso;
    qso_init(&qso);
    strncpy(qso.peer_call, "W5XYZ", sizeof(qso.peer_call) - 1);
    qso.snr_i_sent = 7;
    qso.snr_i_sent_valid = true;
    /* snr_i_got_valid left false -- exchange never actually completed both directions. */

    qso_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.my_call, "KC5CD", sizeof(cfg.my_call) - 1);

    mnemosyne_log_qso(&m, &qso, &cfg, 42ULL);

    TEST_ASSERT_EQUAL_INT(0, s_qso_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_observe_logs_whitelisted_decode);
    RUN_TEST(test_observe_logs_unmatched_cq_from_whitelisted_station);
    RUN_TEST(test_observe_skips_non_whitelisted_decode);
    RUN_TEST(test_observe_dedups_within_a_slot);
    RUN_TEST(test_log_qso_emits_when_both_snr_directions_valid);
    RUN_TEST(test_log_qso_is_noop_when_snr_incomplete);
    return UNITY_END();
}

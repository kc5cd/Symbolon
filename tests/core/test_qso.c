#include "unity.h"
#include "qso.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Phase 3's own test gate (kickoff's phasing table): "qso.c sequence tests driven by
   synthetic decodes" -- no live audio/CAT needed, matching cerberus.c's own table-driven test
   approach at the same argus_decode_t boundary. */

static argus_decode_t make_decode(const char* text, const char* call_to, const char* call_de,
                                  const char* extra, float snr_db)
{
    argus_decode_t d;
    memset(&d, 0, sizeof(d));
    strncpy(d.text, text, sizeof(d.text) - 1);
    strncpy(d.call_to, call_to, sizeof(d.call_to) - 1);
    strncpy(d.call_de, call_de, sizeof(d.call_de) - 1);
    strncpy(d.extra, extra, sizeof(d.extra) - 1);
    d.snr_db = snr_db;
    return d;
}

static qso_config_t make_config(void)
{
    qso_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.my_call, "KC5CD", sizeof(cfg.my_call) - 1);
    strncpy(cfg.my_grid, "EM10", sizeof(cfg.my_grid) - 1);
    return cfg;
}

static cerberus_config_t make_cerberus_config(void)
{
    cerberus_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.my_call, "KC5CD", sizeof(cfg.my_call) - 1);
    strncpy(cfg.whitelist[0], "W5XYZ", sizeof(cfg.whitelist[0]) - 1);
    cfg.whitelist_count = 1;
    return cfg;
}

/* --- Role B: I reply to his CQ (the kickoff's own literal script) --- */

static void test_role_b_full_sequence(void)
{
    qso_t qso;
    qso_init(&qso);
    qso_config_t cfg = make_config();
    cerberus_config_t ccfg = make_cerberus_config();

    /* him: CQ W5XYZ EM12 */
    argus_decode_t cq = make_decode("CQ W5XYZ EM12", "CQ", "W5XYZ", "EM12", 10.0f);
    TEST_ASSERT_TRUE(qso_on_cq_heard(&qso, &cfg, &ccfg, &cq));
    TEST_ASSERT_EQUAL(QSO_STEP_PENDING_CONFIRM, qso.step);
    TEST_ASSERT_EQUAL(QSO_ROLE_B, qso.role);
    TEST_ASSERT_EQUAL_STRING("W5XYZ", qso.peer_call);
    TEST_ASSERT_EQUAL_STRING("W5XYZ KC5CD EM10", qso.pending_text);

    /* operator confirms -> grid sent, awaiting his report */
    qso_confirm_sent(&qso);
    TEST_ASSERT_EQUAL(QSO_STEP_AWAITING_PEER, qso.step);

    /* him: KC5CD W5XYZ -12 (his report of me) */
    argus_decode_t report = make_decode("KC5CD W5XYZ -12", "KC5CD", "W5XYZ", "-12", 7.0f);
    TEST_ASSERT_TRUE(qso_on_decode(&qso, &cfg, &report));
    TEST_ASSERT_EQUAL(QSO_STEP_PENDING_CONFIRM, qso.step);
    TEST_ASSERT_EQUAL_STRING("W5XYZ KC5CD R+07", qso.pending_text);
    TEST_ASSERT_TRUE(qso.snr_i_got_valid);
    TEST_ASSERT_EQUAL_INT(-12, qso.snr_i_got);
    TEST_ASSERT_TRUE(qso.snr_i_sent_valid);
    TEST_ASSERT_EQUAL_INT(7, qso.snr_i_sent);

    /* operator confirms -> R-report sent, awaiting his RR73 */
    qso_confirm_sent(&qso);
    TEST_ASSERT_EQUAL(QSO_STEP_AWAITING_PEER, qso.step);

    /* him: KC5CD W5XYZ RR73 */
    argus_decode_t rr73 = make_decode("KC5CD W5XYZ RR73", "KC5CD", "W5XYZ", "RR73", 8.0f);
    TEST_ASSERT_TRUE(qso_on_decode(&qso, &cfg, &rr73));
    TEST_ASSERT_EQUAL(QSO_STEP_PENDING_CONFIRM, qso.step);
    TEST_ASSERT_EQUAL_STRING("W5XYZ KC5CD 73", qso.pending_text);

    /* operator confirms -> exchange complete, both SNR directions known */
    qso_confirm_sent(&qso);
    TEST_ASSERT_EQUAL(QSO_STEP_COMPLETE, qso.step);
    TEST_ASSERT_EQUAL_INT(-12, qso.snr_i_got);
    TEST_ASSERT_EQUAL_INT(7, qso.snr_i_sent);
}

/* --- Role A: he replies to my CQ / calls me directly with his grid --- */

static void test_role_a_full_sequence(void)
{
    qso_t qso;
    qso_init(&qso);
    qso_config_t cfg = make_config();

    /* him: KC5CD W5XYZ EM12 (his grid, directed at me) */
    argus_decode_t grid = make_decode("KC5CD W5XYZ EM12", "KC5CD", "W5XYZ", "EM12", -8.0f);
    TEST_ASSERT_TRUE(qso_on_decode(&qso, &cfg, &grid));
    TEST_ASSERT_EQUAL(QSO_STEP_PENDING_CONFIRM, qso.step);
    TEST_ASSERT_EQUAL(QSO_ROLE_A, qso.role);
    TEST_ASSERT_EQUAL_STRING("W5XYZ KC5CD -08", qso.pending_text);
    TEST_ASSERT_TRUE(qso.snr_i_sent_valid);
    TEST_ASSERT_EQUAL_INT(-8, qso.snr_i_sent);

    /* operator confirms -> report sent, awaiting his R-report */
    qso_confirm_sent(&qso);
    TEST_ASSERT_EQUAL(QSO_STEP_AWAITING_PEER, qso.step);

    /* him: KC5CD W5XYZ R-03 */
    argus_decode_t rreport = make_decode("KC5CD W5XYZ R-03", "KC5CD", "W5XYZ", "R-03", -6.0f);
    TEST_ASSERT_TRUE(qso_on_decode(&qso, &cfg, &rreport));
    TEST_ASSERT_EQUAL(QSO_STEP_PENDING_CONFIRM, qso.step);
    TEST_ASSERT_EQUAL_STRING("W5XYZ KC5CD RR73", qso.pending_text);
    TEST_ASSERT_TRUE(qso.snr_i_got_valid);
    TEST_ASSERT_EQUAL_INT(-3, qso.snr_i_got);

    /* operator confirms -> complete immediately (role A doesn't wait for his final 73) */
    qso_confirm_sent(&qso);
    TEST_ASSERT_EQUAL(QSO_STEP_COMPLETE, qso.step);
    TEST_ASSERT_EQUAL_INT(-8, qso.snr_i_sent);
    TEST_ASSERT_EQUAL_INT(-3, qso.snr_i_got);

    /* a stray courtesy 73 arriving afterward is a harmless no-op */
    argus_decode_t seventy3 = make_decode("KC5CD W5XYZ 73", "KC5CD", "W5XYZ", "73", -5.0f);
    TEST_ASSERT_FALSE(qso_on_decode(&qso, &cfg, &seventy3));
    TEST_ASSERT_EQUAL(QSO_STEP_COMPLETE, qso.step);
}

/* --- Guards --- */

static void test_cq_from_non_whitelisted_station_is_ignored(void)
{
    qso_t qso;
    qso_init(&qso);
    qso_config_t cfg = make_config();
    cerberus_config_t ccfg = make_cerberus_config();

    argus_decode_t cq = make_decode("CQ N0CALL EM99", "CQ", "N0CALL", "EM99", 10.0f);
    TEST_ASSERT_FALSE(qso_on_cq_heard(&qso, &cfg, &ccfg, &cq));
    TEST_ASSERT_EQUAL(QSO_STEP_IDLE, qso.step);
}

static void test_cq_while_busy_is_ignored(void)
{
    qso_t qso;
    qso_init(&qso);
    qso_config_t cfg = make_config();
    cerberus_config_t ccfg = make_cerberus_config();

    argus_decode_t cq = make_decode("CQ W5XYZ EM12", "CQ", "W5XYZ", "EM12", 10.0f);
    TEST_ASSERT_TRUE(qso_on_cq_heard(&qso, &cfg, &ccfg, &cq));

    /* Already mid-exchange (PENDING_CONFIRM) -- a second CQ must not restart anything. */
    TEST_ASSERT_FALSE(qso_on_cq_heard(&qso, &cfg, &ccfg, &cq));
    TEST_ASSERT_EQUAL(QSO_STEP_PENDING_CONFIRM, qso.step);
}

static void test_unexpected_shape_does_not_advance(void)
{
    qso_t qso;
    qso_init(&qso);
    qso_config_t cfg = make_config();
    cerberus_config_t ccfg = make_cerberus_config();

    argus_decode_t cq = make_decode("CQ W5XYZ EM12", "CQ", "W5XYZ", "EM12", 10.0f);
    qso_on_cq_heard(&qso, &cfg, &ccfg, &cq);
    qso_confirm_sent(&qso); /* AWAITING_PEER, pending_action == GRID (expects a plain report) */

    /* He sends RR73 instead of the expected plain report -- shouldn't happen mid-script, but
       must not spuriously advance the state machine if it does. */
    argus_decode_t wrong = make_decode("KC5CD W5XYZ RR73", "KC5CD", "W5XYZ", "RR73", 10.0f);
    TEST_ASSERT_FALSE(qso_on_decode(&qso, &cfg, &wrong));
    TEST_ASSERT_EQUAL(QSO_STEP_AWAITING_PEER, qso.step);
}

static void test_missed_confirm_leaves_pending_text_for_reoffer(void)
{
    qso_t qso;
    qso_init(&qso);
    qso_config_t cfg = make_config();
    cerberus_config_t ccfg = make_cerberus_config();

    argus_decode_t cq = make_decode("CQ W5XYZ EM12", "CQ", "W5XYZ", "EM12", 10.0f);
    qso_on_cq_heard(&qso, &cfg, &ccfg, &cq);
    char pending_before[QSO_TEXT_MAX];
    strcpy(pending_before, qso.pending_text);

    qso_confirm_missed(&qso);

    /* Per the kickoff: skip the slot, re-offer -- never transmit late. State and pending text
       are unchanged so the same reply gets presented again at the next opportunity. */
    TEST_ASSERT_EQUAL(QSO_STEP_PENDING_CONFIRM, qso.step);
    TEST_ASSERT_EQUAL_STRING(pending_before, qso.pending_text);
}

static void test_reset_allows_a_new_exchange(void)
{
    qso_t qso;
    qso_init(&qso);
    qso_config_t cfg = make_config();

    argus_decode_t grid = make_decode("KC5CD W5XYZ EM12", "KC5CD", "W5XYZ", "EM12", -8.0f);
    qso_on_decode(&qso, &cfg, &grid);
    qso_confirm_sent(&qso);
    argus_decode_t rreport = make_decode("KC5CD W5XYZ R-03", "KC5CD", "W5XYZ", "R-03", -6.0f);
    qso_on_decode(&qso, &cfg, &rreport);
    qso_confirm_sent(&qso);
    TEST_ASSERT_EQUAL(QSO_STEP_COMPLETE, qso.step);

    qso_reset(&qso);
    TEST_ASSERT_EQUAL(QSO_STEP_IDLE, qso.step);
    TEST_ASSERT_EQUAL(QSO_ROLE_NONE, qso.role);
    TEST_ASSERT_FALSE(qso.snr_i_sent_valid);
    TEST_ASSERT_FALSE(qso.snr_i_got_valid);

    /* And it works again from IDLE. */
    argus_decode_t grid2 = make_decode("KC5CD W5XYZ EM12", "KC5CD", "W5XYZ", "EM12", 3.0f);
    TEST_ASSERT_TRUE(qso_on_decode(&qso, &cfg, &grid2));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_role_b_full_sequence);
    RUN_TEST(test_role_a_full_sequence);
    RUN_TEST(test_cq_from_non_whitelisted_station_is_ignored);
    RUN_TEST(test_cq_while_busy_is_ignored);
    RUN_TEST(test_unexpected_shape_does_not_advance);
    RUN_TEST(test_missed_confirm_leaves_pending_text_for_reoffer);
    RUN_TEST(test_reset_allows_a_new_exchange);
    return UNITY_END();
}

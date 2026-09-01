#include "unity.h"
#include "atropos.h"

/* Phase 4's own test gate (kickoff's phasing table): "atropos.c watchdog fires under a
   simulated stall -- as a unit test -- before it's ever verified on the air." This is only
   possible because atropos.c reads time exclusively through the injected sym_host_t seam
   (core/sym_types.h) rather than any real OS clock -- these fixtures drive that clock by
   hand, no sleeping, no wall-clock dependency, fully deterministic. */

static uint64_t s_fake_clock_us;
static int s_ptt_set_call_count;
static int s_last_ptt_value;

static uint64_t fake_mono_us(void* user)
{
    (void)user;
    return s_fake_clock_us;
}

static uint64_t fake_utc_us(void* user)
{
    (void)user;
    return s_fake_clock_us;
}

static sym_rc_t fake_ptt_set(void* user, int assert_tx)
{
    (void)user;
    ++s_ptt_set_call_count;
    s_last_ptt_value = assert_tx;
    return SYM_RC_OK;
}

void setUp(void)
{
    s_fake_clock_us = 0;
    s_ptt_set_call_count = 0;
    s_last_ptt_value = -1;
}

void tearDown(void) {}

static sym_host_t make_fake_host(void)
{
    sym_host_t host;
    host.mono_us = fake_mono_us;
    host.utc_us = fake_utc_us;
    host.ptt_set = fake_ptt_set;
    host.user = NULL;
    return host;
}

/* --- PTT watchdog --- */

static void test_watchdog_does_not_fire_before_threshold(void)
{
    atropos_config_t cfg = { 0 };
    cfg.ptt_watchdog_us = 13500000ULL; /* 13.5s, per the kickoff -- a spec constant */
    sym_host_t host = make_fake_host();
    atropos_t a;
    atropos_init(&a, &cfg, &host);

    atropos_ptt_asserted(&a);
    s_fake_clock_us = 13000000ULL; /* 13.0s elapsed -- still under */

    TEST_ASSERT_FALSE(atropos_watchdog_tick(&a));
    TEST_ASSERT_EQUAL_INT(0, s_ptt_set_call_count);
    TEST_ASSERT_TRUE(a.ptt_asserted);
}

static void test_watchdog_fires_at_threshold(void)
{
    atropos_config_t cfg = { 0 };
    cfg.ptt_watchdog_us = 13500000ULL;
    sym_host_t host = make_fake_host();
    atropos_t a;
    atropos_init(&a, &cfg, &host);

    atropos_ptt_asserted(&a);
    s_fake_clock_us = 13500000ULL; /* exactly at threshold */

    TEST_ASSERT_TRUE(atropos_watchdog_tick(&a));
    TEST_ASSERT_EQUAL_INT(1, s_ptt_set_call_count);
    TEST_ASSERT_EQUAL_INT(0, s_last_ptt_value); /* forced OFF */
    TEST_ASSERT_FALSE(a.ptt_asserted);
}

static void test_watchdog_noop_when_ptt_not_asserted(void)
{
    atropos_config_t cfg = { 0 };
    cfg.ptt_watchdog_us = 13500000ULL;
    sym_host_t host = make_fake_host();
    atropos_t a;
    atropos_init(&a, &cfg, &host);

    s_fake_clock_us = 99999999ULL;
    TEST_ASSERT_FALSE(atropos_watchdog_tick(&a));
    TEST_ASSERT_EQUAL_INT(0, s_ptt_set_call_count);
}

static void test_normal_release_before_threshold_clears_state(void)
{
    atropos_config_t cfg = { 0 };
    cfg.ptt_watchdog_us = 13500000ULL;
    sym_host_t host = make_fake_host();
    atropos_t a;
    atropos_init(&a, &cfg, &host);

    atropos_ptt_asserted(&a);
    s_fake_clock_us = 5000000ULL; /* 5s -- a normal, complete TX */
    atropos_ptt_released(&a);
    TEST_ASSERT_EQUAL_UINT64(5000000ULL, a.total_tx_us_session);

    /* Long after release, ticking must be a harmless no-op -- no stale force-release. */
    s_fake_clock_us = 30000000ULL;
    TEST_ASSERT_FALSE(atropos_watchdog_tick(&a));
    TEST_ASSERT_EQUAL_INT(0, s_ptt_set_call_count);
}

static void test_watchdog_force_release_accumulates_full_overdue_duration(void)
{
    atropos_config_t cfg = { 0 };
    cfg.ptt_watchdog_us = 13500000ULL;
    sym_host_t host = make_fake_host();
    atropos_t a;
    atropos_init(&a, &cfg, &host);

    atropos_ptt_asserted(&a);
    s_fake_clock_us = 20000000ULL; /* stalled well past the watchdog */
    TEST_ASSERT_TRUE(atropos_watchdog_tick(&a));
    TEST_ASSERT_EQUAL_UINT64(20000000ULL, a.total_tx_us_session);
}

/* --- Dead-man timer --- */

static void test_dead_man_does_not_disarm_before_timeout(void)
{
    atropos_config_t cfg = { 0 };
    cfg.dead_man_timeout_us = 600000000ULL; /* 10 minutes */
    sym_host_t host = make_fake_host();
    atropos_t a;
    atropos_init(&a, &cfg, &host);

    atropos_arm(&a);
    s_fake_clock_us = 300000000ULL; /* 5 minutes */
    TEST_ASSERT_FALSE(atropos_dead_man_tick(&a));
    TEST_ASSERT_TRUE(a.armed);
}

static void test_dead_man_disarms_after_timeout(void)
{
    atropos_config_t cfg = { 0 };
    cfg.dead_man_timeout_us = 600000000ULL;
    sym_host_t host = make_fake_host();
    atropos_t a;
    atropos_init(&a, &cfg, &host);

    atropos_arm(&a);
    s_fake_clock_us = 600000000ULL;
    TEST_ASSERT_TRUE(atropos_dead_man_tick(&a));
    TEST_ASSERT_FALSE(a.armed);
}

static void test_operator_input_resets_dead_man_timer(void)
{
    atropos_config_t cfg = { 0 };
    cfg.dead_man_timeout_us = 600000000ULL; /* 10 minutes */
    sym_host_t host = make_fake_host();
    atropos_t a;
    atropos_init(&a, &cfg, &host);

    atropos_arm(&a);
    s_fake_clock_us = 540000000ULL; /* 9 minutes */
    atropos_operator_input(&a);

    s_fake_clock_us = 540000000ULL + 540000000ULL; /* 9 more minutes since input */
    TEST_ASSERT_FALSE(atropos_dead_man_tick(&a));
    TEST_ASSERT_TRUE(a.armed);
}

static void test_dead_man_disabled_when_timeout_zero(void)
{
    atropos_config_t cfg = { 0 }; /* dead_man_timeout_us left at 0 -- disabled */
    sym_host_t host = make_fake_host();
    atropos_t a;
    atropos_init(&a, &cfg, &host);

    atropos_arm(&a);
    s_fake_clock_us = 999999999999ULL;
    TEST_ASSERT_FALSE(atropos_dead_man_tick(&a));
    TEST_ASSERT_TRUE(a.armed);
}

/* --- TX budget --- */

static void test_tx_budget_session_cap(void)
{
    atropos_config_t cfg = { 0 };
    cfg.max_tx_us_session = 60000000ULL; /* 60s hard cap */
    sym_host_t host = make_fake_host();
    atropos_t a;
    atropos_init(&a, &cfg, &host);

    TEST_ASSERT_TRUE(atropos_tx_budget_ok(&a));

    atropos_ptt_asserted(&a);
    s_fake_clock_us = 60000000ULL;
    atropos_ptt_released(&a);

    TEST_ASSERT_FALSE(atropos_tx_budget_ok(&a));
}

static void test_tx_budget_slots_per_hour(void)
{
    atropos_config_t cfg = { 0 };
    cfg.max_tx_slots_per_hour = 3;
    sym_host_t host = make_fake_host();
    atropos_t a;
    atropos_init(&a, &cfg, &host);

    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_TRUE(atropos_tx_budget_ok(&a));
        atropos_ptt_asserted(&a);
        s_fake_clock_us += 15000000ULL; /* one slot apart */
        atropos_ptt_released(&a);
    }

    /* 3 slots recorded within the last hour -- the 4th is refused. */
    TEST_ASSERT_FALSE(atropos_tx_budget_ok(&a));

    /* Once the oldest slot ages out of the rolling hour, budget opens back up. */
    s_fake_clock_us += 3600000000ULL;
    TEST_ASSERT_TRUE(atropos_tx_budget_ok(&a));
}

/* --- Frequency allowlist --- */

static void test_freq_allowed_exact_and_within_tolerance(void)
{
    atropos_config_t cfg = { 0 };
    cfg.allowed_freq_hz[0] = 14074000ULL; /* 20m FT8 dial */
    cfg.allowed_freq_count = 1;
    cfg.freq_tolerance_hz = 10ULL;
    sym_host_t host = make_fake_host();
    atropos_t a;
    atropos_init(&a, &cfg, &host);

    TEST_ASSERT_TRUE(atropos_freq_allowed(&a, 14074000ULL));
    TEST_ASSERT_TRUE(atropos_freq_allowed(&a, 14074005ULL));
    TEST_ASSERT_FALSE(atropos_freq_allowed(&a, 14074011ULL));
    TEST_ASSERT_FALSE(atropos_freq_allowed(&a, 7074000ULL)); /* a different band entirely */
}

static void test_freq_allowlist_empty_fails_closed(void)
{
    atropos_config_t cfg = { 0 }; /* allowed_freq_count left at 0 */
    sym_host_t host = make_fake_host();
    atropos_t a;
    atropos_init(&a, &cfg, &host);

    TEST_ASSERT_FALSE(atropos_freq_allowed(&a, 14074000ULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_watchdog_does_not_fire_before_threshold);
    RUN_TEST(test_watchdog_fires_at_threshold);
    RUN_TEST(test_watchdog_noop_when_ptt_not_asserted);
    RUN_TEST(test_normal_release_before_threshold_clears_state);
    RUN_TEST(test_watchdog_force_release_accumulates_full_overdue_duration);
    RUN_TEST(test_dead_man_does_not_disarm_before_timeout);
    RUN_TEST(test_dead_man_disarms_after_timeout);
    RUN_TEST(test_operator_input_resets_dead_man_timer);
    RUN_TEST(test_dead_man_disabled_when_timeout_zero);
    RUN_TEST(test_tx_budget_session_cap);
    RUN_TEST(test_tx_budget_slots_per_hour);
    RUN_TEST(test_freq_allowed_exact_and_within_tolerance);
    RUN_TEST(test_freq_allowlist_empty_fails_closed);
    return UNITY_END();
}

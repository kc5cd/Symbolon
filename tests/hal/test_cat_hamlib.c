#include "unity.h"
#include "hal_cat.h"

#include <hamlib/rig.h>

void setUp(void) {}
void tearDown(void) {}

/* Phase 2's own test gate (see the kickoff's phasing table): CAT exercised against a Hamlib
   dummy rig, not real hardware. RIG_MODEL_DUMMY does no real serial I/O -- it just echoes
   back whatever's set, in-process, so this runs with no radio, no COM port, no rigctld. */
static hal_cat_t* open_dummy(void)
{
    hal_cat_config_t config = { 0 };
    config.rig_model = RIG_MODEL_DUMMY;

    hal_cat_t* cat = NULL;
    hal_rc_t rc = hal_cat_open(&cat, &config);
    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, rc);
    TEST_ASSERT_NOT_NULL(cat);
    return cat;
}

static void test_open_close(void)
{
    hal_cat_t* cat = open_dummy();
    hal_cat_close(cat);
}

static void test_freq_roundtrip(void)
{
    hal_cat_t* cat = open_dummy();

    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_set_freq_hz(cat, 14074000));

    uint64_t hz = 0;
    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_get_freq_hz(cat, &hz));
    TEST_ASSERT_EQUAL_UINT64(14074000, hz);

    hal_cat_close(cat);
}

static void test_mode_roundtrip(void)
{
    hal_cat_t* cat = open_dummy();

    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_set_mode(cat, HAL_CAT_MODE_USB));

    hal_cat_mode_t mode = HAL_CAT_MODE_LSB;
    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_get_mode(cat, &mode));
    TEST_ASSERT_EQUAL_INT(HAL_CAT_MODE_USB, mode);

    hal_cat_close(cat);
}

static void test_data_l_mode_roundtrip(void)
{
    hal_cat_t* cat = open_dummy();

    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_set_mode(cat, HAL_CAT_MODE_DATA_L));

    hal_cat_mode_t mode = HAL_CAT_MODE_USB;
    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_get_mode(cat, &mode));
    TEST_ASSERT_EQUAL_INT(HAL_CAT_MODE_DATA_L, mode);

    hal_cat_close(cat);
}

static void test_preamp_roundtrip(void)
{
    hal_cat_t* cat = open_dummy();

    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_set_preamp(cat, true));
    bool enabled = false;
    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_get_preamp(cat, &enabled));
    TEST_ASSERT_TRUE(enabled);

    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_set_preamp(cat, false));
    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_get_preamp(cat, &enabled));
    TEST_ASSERT_FALSE(enabled);

    hal_cat_close(cat);
}

static void test_agc_roundtrip(void)
{
    hal_cat_t* cat = open_dummy();

    hal_cat_agc_t levels[] = { HAL_CAT_AGC_SLOW, HAL_CAT_AGC_FAST, HAL_CAT_AGC_AUTO, HAL_CAT_AGC_OFF };
    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
        TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_set_agc(cat, levels[i]));
        hal_cat_agc_t agc = HAL_CAT_AGC_OFF;
        TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_get_agc(cat, &agc));
        TEST_ASSERT_EQUAL_INT(levels[i], agc);
    }

    hal_cat_close(cat);
}

static void test_power_roundtrip(void)
{
    hal_cat_t* cat = open_dummy();

    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_set_freq_hz(cat, 14074000));
    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_set_mode(cat, HAL_CAT_MODE_USB));

    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_set_power_watts(cat, 5.0f));
    float watts = 0.0f;
    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_get_power_watts(cat, &watts));
    /* mW<->fraction round-trip through the dummy rig's own max-power scale isn't exact to
       the mW -- half-watt tolerance matches the 0.5W granularity this is wired for. */
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 5.0f, watts);

    hal_cat_close(cat);
}

static void test_power_override_roundtrip(void)
{
    /* Exercises the max_tx_power_watts override path (see hal_cat.h) rather than Hamlib's
       own rig_mW2power()/rig_power2mW() -- confirmed against real X6200 hardware that its
       Hamlib backend's own conversion is unusable, see context.md's 2026-09-01 entry. */
    hal_cat_config_t config = { 0 };
    config.rig_model = RIG_MODEL_DUMMY;
    config.max_tx_power_watts = 8.0f;

    hal_cat_t* cat = NULL;
    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_open(&cat, &config));

    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_set_power_watts(cat, 4.0f));
    float watts = 0.0f;
    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_get_power_watts(cat, &watts));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 4.0f, watts);

    hal_cat_close(cat);
}

static void test_ptt_roundtrip(void)
{
    hal_cat_t* cat = open_dummy();

    bool asserted = true;
    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_get_ptt(cat, &asserted));
    TEST_ASSERT_FALSE(asserted);

    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_set_ptt(cat, true));
    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_get_ptt(cat, &asserted));
    TEST_ASSERT_TRUE(asserted);

    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_set_ptt(cat, false));
    TEST_ASSERT_EQUAL_INT(HAL_RC_OK, hal_cat_get_ptt(cat, &asserted));
    TEST_ASSERT_FALSE(asserted);

    hal_cat_close(cat);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_open_close);
    RUN_TEST(test_freq_roundtrip);
    RUN_TEST(test_mode_roundtrip);
    RUN_TEST(test_data_l_mode_roundtrip);
    RUN_TEST(test_preamp_roundtrip);
    RUN_TEST(test_agc_roundtrip);
    RUN_TEST(test_power_roundtrip);
    RUN_TEST(test_power_override_roundtrip);
    RUN_TEST(test_ptt_roundtrip);
    return UNITY_END();
}

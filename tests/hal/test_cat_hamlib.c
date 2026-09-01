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
    RUN_TEST(test_ptt_roundtrip);
    return UNITY_END();
}

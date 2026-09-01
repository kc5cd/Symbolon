#include "unity.h"
#include "horae.h"

void setUp(void) {}
void tearDown(void) {}

static void test_epoch_start_is_slot_zero(void)
{
    horae_slot_t s = horae_slot_at(0);
    TEST_ASSERT_EQUAL_UINT64(0, s.slot_epoch_us);
    TEST_ASSERT_EQUAL_UINT64(0, s.offset_us);
}

static void test_exact_slot_boundary_has_zero_offset(void)
{
    /* third slot boundary: 2 * HORAE_SLOT_US */
    horae_slot_t s = horae_slot_at(2 * HORAE_SLOT_US);
    TEST_ASSERT_EQUAL_UINT64(2 * HORAE_SLOT_US, s.slot_epoch_us);
    TEST_ASSERT_EQUAL_UINT64(0, s.offset_us);
}

static void test_mid_slot_offset_is_correct(void)
{
    uint64_t utc_us = 2 * HORAE_SLOT_US + 7500000ULL; /* 7.5 s into the third slot */
    horae_slot_t s = horae_slot_at(utc_us);
    TEST_ASSERT_EQUAL_UINT64(2 * HORAE_SLOT_US, s.slot_epoch_us);
    TEST_ASSERT_EQUAL_UINT64(7500000ULL, s.offset_us);
}

static void test_last_microsecond_of_slot_stays_in_that_slot(void)
{
    uint64_t utc_us = 3 * HORAE_SLOT_US - 1; /* one microsecond before the next boundary */
    horae_slot_t s = horae_slot_at(utc_us);
    TEST_ASSERT_EQUAL_UINT64(2 * HORAE_SLOT_US, s.slot_epoch_us);
    TEST_ASSERT_EQUAL_UINT64(HORAE_SLOT_US - 1, s.offset_us);
}

static void test_first_microsecond_of_next_slot_advances_epoch(void)
{
    uint64_t utc_us = 3 * HORAE_SLOT_US; /* the boundary itself */
    horae_slot_t s = horae_slot_at(utc_us);
    TEST_ASSERT_EQUAL_UINT64(3 * HORAE_SLOT_US, s.slot_epoch_us);
    TEST_ASSERT_EQUAL_UINT64(0, s.offset_us);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_epoch_start_is_slot_zero);
    RUN_TEST(test_exact_slot_boundary_has_zero_offset);
    RUN_TEST(test_mid_slot_offset_is_correct);
    RUN_TEST(test_last_microsecond_of_slot_stays_in_that_slot);
    RUN_TEST(test_first_microsecond_of_next_slot_advances_epoch);
    return UNITY_END();
}

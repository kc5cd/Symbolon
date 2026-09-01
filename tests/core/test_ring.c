#include "unity.h"
#include "ring.h"

void setUp(void) {}
void tearDown(void) {}

static void test_write_then_read_roundtrip(void)
{
    float backing[8];
    sym_ring_t ring;
    sym_ring_init(&ring, backing, 8);

    float in[5] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };
    TEST_ASSERT_EQUAL_UINT(5, sym_ring_write(&ring, in, 5));
    TEST_ASSERT_EQUAL_UINT(5, sym_ring_available(&ring));

    float out[5] = { 0 };
    TEST_ASSERT_EQUAL_UINT(5, sym_ring_read(&ring, out, 5));
    TEST_ASSERT_EQUAL_UINT(0, sym_ring_available(&ring));
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_FLOAT(in[i], out[i]);
    }
}

static void test_write_past_capacity_truncates(void)
{
    float backing[4];
    sym_ring_t ring;
    sym_ring_init(&ring, backing, 4);

    float in[6] = { 1, 2, 3, 4, 5, 6 };
    TEST_ASSERT_EQUAL_UINT(4, sym_ring_write(&ring, in, 6));
    TEST_ASSERT_EQUAL_UINT(4, sym_ring_available(&ring));
}

static void test_read_past_available_truncates(void)
{
    float backing[8];
    sym_ring_t ring;
    sym_ring_init(&ring, backing, 8);

    float in[3] = { 1, 2, 3 };
    sym_ring_write(&ring, in, 3);

    float out[8] = { 0 };
    TEST_ASSERT_EQUAL_UINT(3, sym_ring_read(&ring, out, 8));
    TEST_ASSERT_EQUAL_UINT(0, sym_ring_available(&ring));
}

static void test_wraparound_after_partial_drain(void)
{
    float backing[4];
    sym_ring_t ring;
    sym_ring_init(&ring, backing, 4);

    float first[3] = { 1, 2, 3 };
    sym_ring_write(&ring, first, 3);

    float drained[2] = { 0 };
    sym_ring_read(&ring, drained, 2); /* leaves sample "3" unread, 1 free slot */

    /* Writing 3 more should wrap the backing index around the 4-slot buffer -- capacity is
       4, 1 sample (the "3") is still unread, so only 3 of these should fit. */
    float second[3] = { 4, 5, 6 };
    TEST_ASSERT_EQUAL_UINT(3, sym_ring_write(&ring, second, 3));
    TEST_ASSERT_EQUAL_UINT(4, sym_ring_available(&ring));

    float out[4] = { 0 };
    TEST_ASSERT_EQUAL_UINT(4, sym_ring_read(&ring, out, 4));
    TEST_ASSERT_EQUAL_FLOAT(3, out[0]);
    TEST_ASSERT_EQUAL_FLOAT(4, out[1]);
    TEST_ASSERT_EQUAL_FLOAT(5, out[2]);
    TEST_ASSERT_EQUAL_FLOAT(6, out[3]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_write_then_read_roundtrip);
    RUN_TEST(test_write_past_capacity_truncates);
    RUN_TEST(test_read_past_available_truncates);
    RUN_TEST(test_wraparound_after_partial_drain);
    return UNITY_END();
}

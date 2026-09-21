#include <string.h>

#include "unity.h"
#include "can_codec.h"

void setUp(void) {}
void tearDown(void) {}

static void test_raw_bits_little_endian(void)
{
    uint8_t d[8] = {0};
    can_set_raw(d, 12u, 12u, 0xABCu);
    TEST_ASSERT_EQUAL_HEX8(0xC0u, d[1]);
    TEST_ASSERT_EQUAL_HEX8(0xABu, d[2]);
    TEST_ASSERT_EQUAL_HEX64(0xABCu, can_get_raw(d, 12u, 12u));
    can_set_raw(d, 0u, 64u, 0x0102030405060708ull);
    TEST_ASSERT_EQUAL_HEX64(0x0102030405060708ull, can_get_raw(d, 0u, 64u));
}

static void test_set_raw_preserves_neighbours(void)
{
    uint8_t d[8];
    memset(d, 0xFF, sizeof(d));
    can_set_raw(d, 4u, 8u, 0u);
    TEST_ASSERT_EQUAL_HEX8(0x0Fu, d[0]);
    TEST_ASSERT_EQUAL_HEX8(0xF0u, d[1]);
}

static void test_signed_signal_and_saturation(void)
{
    const can_message_t *m = can_find_message(CAN_ID_PACK_STATUS);
    TEST_ASSERT_NOT_NULL(m);
    const can_signal_t *cur = &m->signals[SIG_PACK_I];
    uint8_t d[8] = {0};
    can_signal_set(cur, d, -250.0);
    TEST_ASSERT_EQUAL_INT64(-2500, can_signal_raw(cur, d));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -250.0f, can_signal_get(cur, d));
    can_signal_set(cur, d, 1e6);
    TEST_ASSERT_EQUAL_INT64(32767, can_signal_raw(cur, d));
    can_signal_set(cur, d, -1e6);
    TEST_ASSERT_EQUAL_INT64(-32768, can_signal_raw(cur, d));
}

static void test_unsigned_offset_signal(void)
{
    const can_message_t *m = can_find_message(CAN_ID_CELL_TEMPS);
    const can_signal_t *t = &m->signals[SIG_T_CELL_B];
    uint8_t d[8] = {0};
    can_signal_set(t, d, 35.04);
    TEST_ASSERT_EQUAL_INT64(750, can_signal_raw(t, d));
    can_signal_set(t, d, -100.0);
    TEST_ASSERT_EQUAL_INT64(0, can_signal_raw(t, d));
    can_signal_set(t, d, 1000.0);
    TEST_ASSERT_EQUAL_INT64(4095, can_signal_raw(t, d));
}

static void test_message_lookup(void)
{
    TEST_ASSERT_NULL(can_find_message(0x7FFu));
    for (size_t i = 0; i < CAN_MESSAGES_LEN; i++) {
        const can_message_t *m = &CAN_MESSAGES[i];
        TEST_ASSERT_EQUAL_PTR(m, can_find_message(m->id));
        uint32_t bits = 0u;
        for (size_t s = 0; s < m->n_signals; s++) {
            TEST_ASSERT_TRUE(m->signals[s].start + m->signals[s].length <= 64u);
            bits += m->signals[s].length;
        }
        TEST_ASSERT_TRUE(bits <= 64u);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_raw_bits_little_endian);
    RUN_TEST(test_set_raw_preserves_neighbours);
    RUN_TEST(test_signed_signal_and_saturation);
    RUN_TEST(test_unsigned_offset_signal);
    RUN_TEST(test_message_lookup);
    return UNITY_END();
}

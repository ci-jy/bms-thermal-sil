#include <string.h>

#include "unity.h"
#include "e2e.h"

void setUp(void) {}
void tearDown(void) {}

static void test_crc8_sae_j1850_check_value(void)
{
    const uint8_t msg[] = "123456789";
    /* SAE J1850 CRC-8: poly 0x1D, init 0xFF, xorout 0xFF -> check 0x4B */
    TEST_ASSERT_EQUAL_HEX8(0x4Bu, (uint8_t)(crc8_j1850(msg, 9u, 0xFFu) ^ 0xFFu));
}

static void frame(uint8_t d[8], uint8_t counter)
{
    memset(d, 0, 8u);
    d[0] = 0x12u;
    d[3] = 0x34u;
    e2e_protect(0x100u, d, counter);
}

static void test_valid_sequence_accepted(void)
{
    e2e_rx_t rx;
    e2e_rx_init(&rx, 100u, 2u, 0u);
    uint8_t d[8];
    for (uint8_t i = 0; i < 40u; i++) { /* wraps the 4-bit counter */
        frame(d, (uint8_t)(i & 0x0Fu));
        TEST_ASSERT_EQUAL(E2E_OK, e2e_rx_check(&rx, 0x100u, d, i * 10u));
    }
    TEST_ASSERT_TRUE(rx.ever_ok);
}

static void test_crc_error_detected(void)
{
    e2e_rx_t rx;
    e2e_rx_init(&rx, 100u, 2u, 0u);
    uint8_t d[8];
    frame(d, 1u);
    d[2] ^= 0x01u;
    TEST_ASSERT_EQUAL(E2E_ERR_CRC, e2e_rx_check(&rx, 0x100u, d, 0u));
    frame(d, 1u);
    /* Same payload under a different CAN ID fails (data ID in the CRC). */
    TEST_ASSERT_EQUAL(E2E_ERR_CRC, e2e_rx_check(&rx, 0x110u, d, 0u));
}

static void test_repeated_and_sequence_errors(void)
{
    e2e_rx_t rx;
    e2e_rx_init(&rx, 100u, 2u, 0u);
    uint8_t d[8];
    frame(d, 3u);
    TEST_ASSERT_EQUAL(E2E_OK, e2e_rx_check(&rx, 0x100u, d, 0u));
    TEST_ASSERT_EQUAL(E2E_ERR_REPEATED, e2e_rx_check(&rx, 0x100u, d, 10u));
    frame(d, 5u); /* one lost frame is tolerated */
    TEST_ASSERT_EQUAL(E2E_OK, e2e_rx_check(&rx, 0x100u, d, 20u));
    frame(d, 9u);
    TEST_ASSERT_EQUAL(E2E_ERR_SEQUENCE, e2e_rx_check(&rx, 0x100u, d, 30u));
    frame(d, 10u); /* resynchronised */
    TEST_ASSERT_EQUAL(E2E_OK, e2e_rx_check(&rx, 0x100u, d, 40u));
}

static void test_timeout(void)
{
    e2e_rx_t rx;
    e2e_rx_init(&rx, 100u, 2u, 0u);
    TEST_ASSERT_FALSE(e2e_rx_timed_out(&rx, 100u));
    TEST_ASSERT_TRUE(e2e_rx_timed_out(&rx, 101u));
    uint8_t d[8];
    frame(d, 0u);
    (void)e2e_rx_check(&rx, 0x100u, d, 150u);
    TEST_ASSERT_FALSE(e2e_rx_timed_out(&rx, 200u));
    /* A frame stamped ahead of the current time is fresh, not stale. */
    TEST_ASSERT_FALSE(e2e_rx_timed_out(&rx, 140u));
    TEST_ASSERT_TRUE(e2e_rx_timed_out(&rx, 251u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc8_sae_j1850_check_value);
    RUN_TEST(test_valid_sequence_accepted);
    RUN_TEST(test_crc_error_detected);
    RUN_TEST(test_repeated_and_sequence_errors);
    RUN_TEST(test_timeout);
    return UNITY_END();
}

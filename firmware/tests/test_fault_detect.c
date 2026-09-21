#include "unity.h"
#include "bms_config.h"
#include "fault_detect.h"

void setUp(void) {}
void tearDown(void) {}

static void test_debounce_qualifies_holds_and_clears(void)
{
    debounce_t d;
    debounce_init(&d, 2u, 1u, 6u);
    TEST_ASSERT_FALSE(debounce_step(&d, true));  /* 2 */
    TEST_ASSERT_FALSE(debounce_step(&d, true));  /* 4 */
    TEST_ASSERT_TRUE(debounce_step(&d, true));   /* 6 -> active */
    TEST_ASSERT_TRUE(debounce_step(&d, true));   /* saturates at 6 */
    TEST_ASSERT_EQUAL_UINT16(6u, d.count);
    TEST_ASSERT_TRUE(debounce_step(&d, false));  /* 5: hold */
    for (int i = 0; i < 4; i++) {
        (void)debounce_step(&d, false);
    }
    TEST_ASSERT_TRUE(d.active);                  /* 1: still held */
    TEST_ASSERT_FALSE(debounce_step(&d, false)); /* 0: cleared */
    TEST_ASSERT_FALSE(debounce_step(&d, false)); /* floor at 0 */
}

static void test_range_fault_after_debounce(void)
{
    sensor_monitor_t m;
    sensor_monitor_init(&m);
    sensor_status_t s = sensor_monitor_step(&m, 100u, 30.0f);
    TEST_ASSERT_FALSE(sensor_status_faulty(s));
    TEST_ASSERT_TRUE(sensor_monitor_usable(&m, s));
    for (uint32_t i = 0; i < BMS_SENS_RANGE_DEBOUNCE - 1u; i++) {
        s = sensor_monitor_step(&m, (uint16_t)(4095u - i), 369.5f - (float)i * 0.1f);
        TEST_ASSERT_FALSE(s.range);
        TEST_ASSERT_FALSE(sensor_monitor_usable(&m, s)); /* bad sample not used */
    }
    s = sensor_monitor_step(&m, 4000u, 360.0f);
    TEST_ASSERT_TRUE(s.range);
    s = sensor_monitor_step(&m, 0u, -40.0f);
    TEST_ASSERT_TRUE(s.range);
}

static void test_stuck_at_detected_after_n_identical_samples(void)
{
    sensor_monitor_t m;
    sensor_monitor_init(&m);
    sensor_status_t s;
    for (uint32_t i = 0; i < BMS_SENS_STUCK_SAMPLES - 1u; i++) {
        s = sensor_monitor_step(&m, 700u, 30.0f);
        TEST_ASSERT_FALSE(s.stuck);
    }
    s = sensor_monitor_step(&m, 700u, 30.0f);
    TEST_ASSERT_TRUE(s.stuck);
    /* A changing value clears the stuck flag. */
    s = sensor_monitor_step(&m, 701u, 30.1f);
    TEST_ASSERT_FALSE(s.stuck);
}

static void test_noisy_signal_never_stuck(void)
{
    sensor_monitor_t m;
    sensor_monitor_init(&m);
    for (uint16_t i = 0; i < 1000u; i++) {
        const uint16_t raw = (uint16_t)(700u + (i % 3u));
        const sensor_status_t s = sensor_monitor_step(&m, raw, 30.0f + 0.1f * (float)(i % 3u));
        TEST_ASSERT_FALSE(sensor_status_faulty(s));
    }
}

static void test_rate_of_change_fault(void)
{
    sensor_monitor_t m;
    sensor_monitor_init(&m);
    (void)sensor_monitor_step(&m, 700u, 30.0f);
    sensor_status_t s = sensor_monitor_step(&m, 850u, 45.0f); /* +15 */
    TEST_ASSERT_FALSE(s.roc);
    s = sensor_monitor_step(&m, 700u, 30.0f); /* -15 */
    TEST_ASSERT_TRUE(s.roc);
    TEST_ASSERT_TRUE(sensor_status_faulty(s));
}

static void test_plausibility(void)
{
    plausibility_t p;
    plausibility_init(&p, 4.0f, 3u);
    TEST_ASSERT_FALSE(plausibility_step(&p, 30.0f, 31.0f));
    TEST_ASSERT_FALSE(plausibility_step(&p, 30.0f, 36.0f));
    TEST_ASSERT_FALSE(plausibility_step(&p, 36.0f, 30.0f));
    TEST_ASSERT_TRUE(plausibility_step(&p, 36.0f, 30.0f));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_debounce_qualifies_holds_and_clears);
    RUN_TEST(test_range_fault_after_debounce);
    RUN_TEST(test_stuck_at_detected_after_n_identical_samples);
    RUN_TEST(test_noisy_signal_never_stuck);
    RUN_TEST(test_rate_of_change_fault);
    RUN_TEST(test_plausibility);
    return UNITY_END();
}

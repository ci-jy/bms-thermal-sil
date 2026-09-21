#include "unity.h"
#include "derate.h"

void setUp(void) {}
void tearDown(void) {}

static void test_lookup_clamps_and_interpolates(void)
{
    const derate_point_t *m = DERATE_DEFAULT_MAP;
    const size_t n = DERATE_DEFAULT_MAP_LEN;
    TEST_ASSERT_EQUAL_FLOAT(0.15f, derate_lookup(m, n, -20.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, derate_lookup(m, n, 30.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.8f, derate_lookup(m, n, 42.5f));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.6f, derate_lookup(m, n, 45.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, derate_lookup(m, n, 80.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, derate_lookup(m, 0u, 30.0f));
}

static void test_map_is_monotonic_above_20c(void)
{
    float prev = 1.0f;
    for (float t = 20.0f; t <= 60.0f; t += 0.5f) {
        const float f = derate_lookup(DERATE_DEFAULT_MAP, DERATE_DEFAULT_MAP_LEN, t);
        TEST_ASSERT_TRUE(f <= prev + 1e-6f);
        TEST_ASSERT_TRUE(f >= 0.0f && f <= 1.0f);
        prev = f;
    }
}

static void test_hysteresis_holds_inside_band(void)
{
    derate_t d;
    derate_init(&d, DERATE_DEFAULT_MAP, DERATE_DEFAULT_MAP_LEN, 2.0f);
    const float hot = derate_step(&d, 47.0f);
    /* Cooling by less than the band keeps the derated value. */
    TEST_ASSERT_EQUAL_FLOAT(hot, derate_step(&d, 45.5f));
    /* Heating further is tracked immediately. */
    TEST_ASSERT_TRUE(derate_step(&d, 48.0f) < hot);
}

static void test_hysteresis_releases_after_band(void)
{
    derate_t d;
    derate_init(&d, DERATE_DEFAULT_MAP, DERATE_DEFAULT_MAP_LEN, 2.0f);
    (void)derate_step(&d, 47.0f);
    const float f = derate_step(&d, 44.0f); /* evaluated at 46 */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, derate_lookup(DERATE_DEFAULT_MAP, DERATE_DEFAULT_MAP_LEN, 46.0f), f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, derate_step(&d, 30.0f));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_lookup_clamps_and_interpolates);
    RUN_TEST(test_map_is_monotonic_above_20c);
    RUN_TEST(test_hysteresis_holds_inside_band);
    RUN_TEST(test_hysteresis_releases_after_band);
    return UNITY_END();
}

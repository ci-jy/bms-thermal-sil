#include "unity.h"
#include "filter.h"
#include "pi_ctrl.h"

void setUp(void) {}
void tearDown(void) {}

static void test_clampf(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, clampf(-1.0f, 0.0f, 1.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, clampf(2.0f, 0.0f, 1.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, clampf(0.5f, 0.0f, 1.0f));
}

static void test_lowpass_primes_on_first_sample_and_converges(void)
{
    lowpass_t f;
    lowpass_init(&f, 0.1f, 0.01f);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, lowpass_step(&f, 30.0f));
    float y = 0.0f;
    for (int i = 0; i < 200; i++) {
        y = lowpass_step(&f, 40.0f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, y);
}

static void test_lowpass_zero_tau_passes_through(void)
{
    lowpass_t f;
    lowpass_init(&f, 0.0f, 0.01f);
    (void)lowpass_step(&f, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, lowpass_step(&f, 5.0f));
}

static void test_rate_limiter_limits_both_directions(void)
{
    rate_limiter_t r;
    rate_limiter_init(&r, 0.5f, 0.01f, 0.0f); /* 0.005 per step */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.005f, rate_limiter_step(&r, 1.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, rate_limiter_step(&r, -1.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, rate_limiter_step(&r, 0.0f));
}

static void test_pi_proportional_and_integral(void)
{
    pi_ctrl_t pi;
    pi_init(&pi, 0.3f, 0.1f, 0.01f, 0.0f, 1.0f);
    const float u1 = pi_step(&pi, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.3f + 0.001f, u1);
    const float u2 = pi_step(&pi, 1.0f);
    TEST_ASSERT_TRUE(u2 > u1);
    TEST_ASSERT_FALSE(pi.saturated);
}

static void test_pi_output_clamped(void)
{
    pi_ctrl_t pi;
    pi_init(&pi, 1.0f, 0.0f, 0.01f, 0.0f, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, pi_step(&pi, 5.0f));
    TEST_ASSERT_TRUE(pi.saturated);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pi_step(&pi, -5.0f));
    TEST_ASSERT_TRUE(pi.saturated);
}

static void test_pi_anti_windup_high(void)
{
    pi_ctrl_t pi;
    pi_init(&pi, 0.5f, 1.0f, 0.01f, 0.0f, 1.0f);
    for (int i = 0; i < 10000; i++) {
        (void)pi_step(&pi, 10.0f); /* hard saturation for 100 s */
    }
    TEST_ASSERT_TRUE(pi.integ <= 1.0f);
    /* Once the error reverses, output leaves saturation immediately. */
    const float u = pi_step(&pi, -1.0f);
    TEST_ASSERT_TRUE(u < 1.0f);
}

static void test_pi_anti_windup_low(void)
{
    pi_ctrl_t pi;
    pi_init(&pi, 0.5f, 1.0f, 0.01f, 0.0f, 1.0f);
    for (int i = 0; i < 1000; i++) {
        (void)pi_step(&pi, -10.0f);
    }
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pi.integ);
    TEST_ASSERT_TRUE(pi_step(&pi, 1.0f) > 0.0f);
}

static void test_pi_integrates_when_saturated_but_error_reverses(void)
{
    pi_ctrl_t pi;
    pi_init(&pi, 2.0f, 1.0f, 0.1f, 0.0f, 1.0f);
    pi.integ = 0.5f;
    /* p = 2 * 0.6 -> saturated high, error positive: integrator frozen */
    (void)pi_step(&pi, 0.6f);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, pi.integ);
    /* p = -1.2 -> saturated low, error negative: frozen as well */
    (void)pi_step(&pi, -0.6f);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, pi.integ);
    pi_reset(&pi);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pi.integ);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_clampf);
    RUN_TEST(test_lowpass_primes_on_first_sample_and_converges);
    RUN_TEST(test_lowpass_zero_tau_passes_through);
    RUN_TEST(test_rate_limiter_limits_both_directions);
    RUN_TEST(test_pi_proportional_and_integral);
    RUN_TEST(test_pi_output_clamped);
    RUN_TEST(test_pi_anti_windup_high);
    RUN_TEST(test_pi_anti_windup_low);
    RUN_TEST(test_pi_integrates_when_saturated_but_error_reverses);
    return UNITY_END();
}

#include "unity.h"
#include "bms_config.h"
#include "state_machine.h"

static state_machine_t sm;

void setUp(void) { sm_init(&sm); }
void tearDown(void) {}

static bms_state_t step(bool valid, float t, uint16_t flags)
{
    sm_inputs_t in = {valid, t, flags};
    return sm_step(&sm, &in);
}

static void reach_normal(void)
{
    for (uint32_t i = 0; i < SM_INIT_VALID_CYCLES; i++) {
        (void)step(true, 30.0f, 0u);
    }
    TEST_ASSERT_EQUAL(BMS_NORMAL, sm.state);
}

static void test_init_requires_consecutive_valid_cycles(void)
{
    for (uint32_t i = 0; i < SM_INIT_VALID_CYCLES - 1u; i++) {
        TEST_ASSERT_EQUAL(BMS_INIT, step(true, 30.0f, 0u));
    }
    TEST_ASSERT_EQUAL(BMS_INIT, step(false, 30.0f, 0u)); /* counter resets */
    TEST_ASSERT_EQUAL(BMS_INIT, step(true, 30.0f, 0u));
    reach_normal();
}

static void test_fault_reaction_classes(void)
{
    TEST_ASSERT_EQUAL(BMS_NORMAL, sm_fault_reaction(0u));
    TEST_ASSERT_EQUAL(BMS_DERATE, sm_fault_reaction(FAULT_SENS_A_STUCK));
    TEST_ASSERT_EQUAL(BMS_DERATE, sm_fault_reaction(FAULT_SENS_B_RANGE));
    TEST_ASSERT_EQUAL(BMS_DERATE, sm_fault_reaction(FAULT_SENS_COOLANT));
    TEST_ASSERT_EQUAL(BMS_LIMP, sm_fault_reaction(FAULT_SENS_AB_PLAUS));
    TEST_ASSERT_EQUAL(BMS_LIMP, sm_fault_reaction(FAULT_E2E_CRC));
    TEST_ASSERT_EQUAL(BMS_LIMP, sm_fault_reaction(FAULT_E2E_ALIVE));
    TEST_ASSERT_EQUAL(BMS_LIMP, sm_fault_reaction(FAULT_CAN_TIMEOUT_PACK));
    TEST_ASSERT_EQUAL(BMS_SAFE_STATE, sm_fault_reaction(FAULT_CAN_TIMEOUT_TEMPS));
    TEST_ASSERT_EQUAL(BMS_SAFE_STATE, sm_fault_reaction(FAULT_OVERTEMP));
    TEST_ASSERT_EQUAL(BMS_SAFE_STATE, sm_fault_reaction(FAULT_WATCHDOG));
    TEST_ASSERT_EQUAL(BMS_SAFE_STATE, sm_fault_reaction(FAULT_SENS_A_RANGE | FAULT_SENS_B_ROC));
}

static void test_derate_fault_latches(void)
{
    reach_normal();
    TEST_ASSERT_EQUAL(BMS_DERATE, step(true, 30.0f, FAULT_SENS_A_STUCK));
    TEST_ASSERT_EQUAL(BMS_DERATE, step(true, 30.0f, 0u)); /* fault gone, reaction kept */
    TEST_ASSERT_EQUAL_HEX16(FAULT_SENS_A_STUCK, sm.latched_flags);
}

static void test_escalation_and_safe_state_latch(void)
{
    reach_normal();
    TEST_ASSERT_EQUAL(BMS_LIMP, step(true, 30.0f, FAULT_E2E_CRC));
    TEST_ASSERT_EQUAL(BMS_LIMP, step(true, 30.0f, FAULT_SENS_A_STUCK)); /* no downgrade */
    TEST_ASSERT_EQUAL(BMS_SAFE_STATE, step(true, 30.0f, FAULT_CAN_TIMEOUT_TEMPS));
    TEST_ASSERT_EQUAL(BMS_SAFE_STATE, step(true, 30.0f, 0u));
}

static void test_safe_state_reachable_from_init(void)
{
    TEST_ASSERT_EQUAL(BMS_SAFE_STATE, step(false, 0.0f, FAULT_CAN_TIMEOUT_TEMPS));
}

static void test_thermal_derate_with_hysteresis(void)
{
    reach_normal();
    TEST_ASSERT_EQUAL(BMS_NORMAL, step(true, BMS_T_DERATE_STATE_C, 0u));
    TEST_ASSERT_EQUAL(BMS_DERATE, step(true, BMS_T_DERATE_STATE_C + 0.1f, 0u));
    TEST_ASSERT_EQUAL(BMS_DERATE, step(true, BMS_T_DERATE_EXIT_C + 0.5f, 0u));
    TEST_ASSERT_EQUAL(BMS_NORMAL, step(true, BMS_T_DERATE_EXIT_C - 0.1f, 0u));
    TEST_ASSERT_EQUAL(0u, sm.latched_flags); /* thermal derate is not a fault */
}

static void test_state_names(void)
{
    TEST_ASSERT_EQUAL_STRING("INIT", sm_state_name(BMS_INIT));
    TEST_ASSERT_EQUAL_STRING("NORMAL", sm_state_name(BMS_NORMAL));
    TEST_ASSERT_EQUAL_STRING("DERATE", sm_state_name(BMS_DERATE));
    TEST_ASSERT_EQUAL_STRING("LIMP", sm_state_name(BMS_LIMP));
    TEST_ASSERT_EQUAL_STRING("SAFE_STATE", sm_state_name(BMS_SAFE_STATE));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", sm_state_name((bms_state_t)7));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_requires_consecutive_valid_cycles);
    RUN_TEST(test_fault_reaction_classes);
    RUN_TEST(test_derate_fault_latches);
    RUN_TEST(test_escalation_and_safe_state_latch);
    RUN_TEST(test_safe_state_reachable_from_init);
    RUN_TEST(test_thermal_derate_with_hysteresis);
    RUN_TEST(test_state_names);
    return UNITY_END();
}

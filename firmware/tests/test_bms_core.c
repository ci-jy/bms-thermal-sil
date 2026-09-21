#include <string.h>

#include "unity.h"
#include "bms_config.h"
#include "bms_core.h"
#include "can_codec.h"

static bms_core_t core;
static uint32_t now;
static uint8_t cnt_temps, cnt_pack;

void setUp(void)
{
    bms_core_init(&core, 0u);
    now = 0u;
    cnt_temps = cnt_pack = 0u;
}
void tearDown(void) {}

static void send_temps(float a, float b, float cin)
{
    const can_message_t *m = can_find_message(CAN_ID_CELL_TEMPS);
    uint8_t d[8] = {0};
    can_signal_set(&m->signals[SIG_T_CELL_A], d, a);
    can_signal_set(&m->signals[SIG_T_CELL_B], d, b);
    can_signal_set(&m->signals[SIG_T_COOL_IN], d, cin);
    can_signal_set(&m->signals[SIG_T_COOL_OUT], d, cin + 1.0f);
    e2e_protect(CAN_ID_CELL_TEMPS, d, cnt_temps++);
    bms_core_on_frame(&core, CAN_ID_CELL_TEMPS, d, now);
}

static void send_pack(void)
{
    const can_message_t *m = can_find_message(CAN_ID_PACK_STATUS);
    uint8_t d[8] = {0};
    can_signal_set(&m->signals[SIG_PACK_I], d, 250.0);
    can_signal_set(&m->signals[SIG_PACK_SOC], d, 40.0);
    e2e_protect(CAN_ID_PACK_STATUS, d, cnt_pack++);
    bms_core_on_frame(&core, CAN_ID_PACK_STATUS, d, now);
}

/* One 10 ms cycle with slightly varying (noisy) sensor readings. */
static const bms_outputs_t *cycle(float t)
{
    const float n = (float)(now / 10u % 3u) * 0.1f;
    send_temps(t + n, t - n, 25.0f + n);
    send_pack();
    const bms_outputs_t *o = bms_core_step(&core, now);
    now += BMS_CONTROL_PERIOD_MS;
    return o;
}

static void test_init_to_normal_and_full_current(void)
{
    const bms_outputs_t *o = bms_core_step(&core, now);
    TEST_ASSERT_EQUAL(BMS_INIT, o->state);
    TEST_ASSERT_FALSE(o->charge_enable);
    for (int i = 0; i < 10; i++) {
        o = cycle(30.0f);
    }
    TEST_ASSERT_EQUAL(BMS_NORMAL, o->state);
    TEST_ASSERT_TRUE(o->charge_enable);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, BMS_I_CHARGE_MAX_A, o->i_req_a);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, BMS_V_CHARGE_MAX_V, o->v_req_v);
    TEST_ASSERT_EQUAL_HEX16(0u, o->fault_flags);
    TEST_ASSERT_TRUE(core.have_pack);
}

static void test_hot_pack_drives_cooling(void)
{
    const bms_outputs_t *o = NULL;
    for (int i = 0; i < 1000; i++) {
        o = cycle(42.0f);
    }
    TEST_ASSERT_TRUE(o->pump > 0.9f);
    TEST_ASSERT_TRUE(o->chiller > 0.5f);
    TEST_ASSERT_TRUE(o->i_req_a < BMS_I_CHARGE_MAX_A);
}

static void test_cold_pack_minimum_pump_no_chiller(void)
{
    const bms_outputs_t *o = NULL;
    for (int i = 0; i < 500; i++) {
        o = cycle(25.0f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, BMS_PUMP_MIN, o->pump);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, o->chiller);
}

static void test_sensor_fault_derates_on_redundant_channel(void)
{
    for (int i = 0; i < 10; i++) {
        (void)cycle(30.0f);
    }
    send_temps(369.5f, 30.0f, 25.0f);
    send_pack();
    const bms_outputs_t *o = bms_core_step(&core, now);
    /* The open-circuit sample is not used even before the fault qualifies. */
    TEST_ASSERT_TRUE(o->t_cell_c < 31.0f);
    for (int i = 0; i < 5; i++) {
        now += 10u;
        send_temps(369.5f, 30.0f + 0.1f * (float)(i % 2), 25.0f + 0.1f * (float)(i % 2));
        send_pack();
        o = bms_core_step(&core, now);
    }
    TEST_ASSERT_EQUAL(BMS_DERATE, o->state);
    TEST_ASSERT_TRUE(o->fault_flags & FAULT_SENS_A_RANGE);
    TEST_ASSERT_TRUE(o->i_req_a <= 0.5f * BMS_I_CHARGE_MAX_A + 0.01f);
    TEST_ASSERT_TRUE(o->latched);
}

static void test_both_sensors_lost_goes_safe(void)
{
    for (int i = 0; i < 10; i++) {
        (void)cycle(30.0f);
    }
    const bms_outputs_t *o = NULL;
    for (int i = 0; i < 4; i++) {
        send_temps(369.5f, -40.0f, 25.0f + 0.1f * (float)(i % 2));
        send_pack();
        o = bms_core_step(&core, now);
        now += 10u;
    }
    TEST_ASSERT_EQUAL(BMS_SAFE_STATE, o->state);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, o->i_req_a);
    TEST_ASSERT_FALSE(o->charge_enable);
}

static void test_implausible_sensors_limp(void)
{
    for (int i = 0; i < 10; i++) {
        (void)cycle(30.0f);
    }
    const bms_outputs_t *o = NULL;
    for (int i = 0; i < 10; i++) {
        const float n = 0.1f * (float)(i % 2);
        send_temps(36.0f + n, 30.0f + n, 25.0f + n);
        send_pack();
        o = bms_core_step(&core, now);
        now += 10u;
    }
    TEST_ASSERT_EQUAL(BMS_LIMP, o->state);
    TEST_ASSERT_TRUE(o->i_req_a <= BMS_I_LIMP_A);
    /* LIMP drives the pump to 100 % (rate limited) */
    for (int i = 0; i < 200; i++) {
        o = cycle(30.0f);
    }
    TEST_ASSERT_EQUAL_FLOAT(1.0f, o->pump);
}

static void test_overtemperature_safe_state(void)
{
    const bms_outputs_t *o = NULL;
    for (int i = 0; i < 200; i++) {
        o = cycle(BMS_T_OVERTEMP_C + 2.0f);
    }
    TEST_ASSERT_EQUAL(BMS_SAFE_STATE, o->state);
    TEST_ASSERT_TRUE(o->fault_flags & FAULT_OVERTEMP);
}

static void test_message_timeout_and_e2e_errors(void)
{
    for (int i = 0; i < 10; i++) {
        (void)cycle(30.0f);
    }
    /* Corrupted CRC on every frame: E2E CRC fault -> LIMP */
    const can_message_t *m = can_find_message(CAN_ID_CELL_TEMPS);
    uint8_t d[8] = {0};
    can_signal_set(&m->signals[SIG_T_CELL_A], d, 30.0);
    e2e_protect(CAN_ID_CELL_TEMPS, d, cnt_temps++);
    d[7] ^= 0xFFu;
    for (int i = 0; i < 3; i++) {
        bms_core_on_frame(&core, CAN_ID_CELL_TEMPS, d, now);
    }
    send_pack();
    const bms_outputs_t *o = bms_core_step(&core, now);
    TEST_ASSERT_TRUE(o->fault_flags & FAULT_E2E_CRC);
    TEST_ASSERT_EQUAL(BMS_LIMP, o->state);
    /* Then no valid temperature frames for > timeout -> SAFE_STATE */
    for (int i = 0; i < 12; i++) {
        now += 10u;
        send_pack();
        o = bms_core_step(&core, now);
    }
    TEST_ASSERT_TRUE(o->fault_flags & FAULT_CAN_TIMEOUT_TEMPS);
    TEST_ASSERT_EQUAL(BMS_SAFE_STATE, o->state);
}

static void test_alive_counter_error_on_pack_status(void)
{
    for (int i = 0; i < 10; i++) {
        (void)cycle(30.0f);
    }
    const bms_outputs_t *o = NULL;
    cnt_pack = 3u;
    for (int i = 0; i < 4; i++) {
        send_temps(30.0f + 0.1f * (float)(i % 2), 30.0f, 25.0f + 0.1f * (float)(i % 2));
        cnt_pack = 3u; /* frozen counter */
        send_pack();
        o = bms_core_step(&core, now);
        now += 10u;
    }
    TEST_ASSERT_TRUE(o->fault_flags & FAULT_E2E_ALIVE);
    TEST_ASSERT_EQUAL(BMS_LIMP, o->state);
}

static void test_watchdog_fault_goes_safe_and_frames_pack(void)
{
    for (int i = 0; i < 10; i++) {
        (void)cycle(30.0f);
    }
    bms_core_report_watchdog_fault(&core);
    const bms_outputs_t *o = cycle(30.0f);
    TEST_ASSERT_EQUAL(BMS_SAFE_STATE, o->state);

    uint8_t d[8];
    bms_core_pack_thermal_cmd(&core, d, 5u);
    const can_message_t *m = can_find_message(CAN_ID_THERMAL_CMD);
    TEST_ASSERT_EQUAL_INT64(BMS_SAFE_STATE, can_signal_raw(&m->signals[SIG_STATE], d));
    TEST_ASSERT_EQUAL_INT64(1, can_signal_raw(&m->signals[SIG_LATCHED], d));
    TEST_ASSERT_EQUAL_HEX8(e2e_crc(CAN_ID_THERMAL_CMD, d), d[7]);
    TEST_ASSERT_EQUAL_UINT8(5u, d[6] & 0x0Fu);

    bms_core_pack_charge_request(&core, d, 1u);
    m = can_find_message(CAN_ID_CHARGE_REQUEST);
    TEST_ASSERT_EQUAL_INT64(0, can_signal_raw(&m->signals[SIG_CHG_EN], d));
    TEST_ASSERT_EQUAL_INT64(0, can_signal_raw(&m->signals[SIG_I_REQ], d));

    bms_core_pack_fault_status(&core, d, 2u, false);
    m = can_find_message(CAN_ID_FAULT_STATUS);
    TEST_ASSERT_TRUE(can_signal_raw(&m->signals[SIG_DIAG_FLAGS], d) & FAULT_WATCHDOG);
    TEST_ASSERT_EQUAL_INT64(0, can_signal_raw(&m->signals[SIG_WDG_OK], d));
}

static void test_unknown_frames_ignored(void)
{
    const uint8_t d[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    bms_core_on_frame(&core, 0x7FFu, d, 0u);
    TEST_ASSERT_FALSE(core.have_temps);
    TEST_ASSERT_FALSE(core.have_pack);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_to_normal_and_full_current);
    RUN_TEST(test_hot_pack_drives_cooling);
    RUN_TEST(test_cold_pack_minimum_pump_no_chiller);
    RUN_TEST(test_sensor_fault_derates_on_redundant_channel);
    RUN_TEST(test_both_sensors_lost_goes_safe);
    RUN_TEST(test_implausible_sensors_limp);
    RUN_TEST(test_overtemperature_safe_state);
    RUN_TEST(test_message_timeout_and_e2e_errors);
    RUN_TEST(test_alive_counter_error_on_pack_status);
    RUN_TEST(test_watchdog_fault_goes_safe_and_frames_pack);
    RUN_TEST(test_unknown_frames_ignored);
    return UNITY_END();
}

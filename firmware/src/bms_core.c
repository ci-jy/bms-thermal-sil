#include "bms_core.h"

#include <string.h>

#include "bms_config.h"
#include "can_codec.h"

#define DT_S ((float)BMS_CONTROL_PERIOD_MS / 1000.0f)

void bms_core_init(bms_core_t *c, uint32_t now_ms)
{
    memset(c, 0, sizeof(*c));
    e2e_rx_init(&c->rx_temps, BMS_E2E_TIMEOUT_MS, BMS_E2E_MAX_DELTA, now_ms);
    e2e_rx_init(&c->rx_pack, BMS_E2E_TIMEOUT_MS, BMS_E2E_MAX_DELTA, now_ms);
    debounce_init(&c->e2e_crc, BMS_E2E_ERR_INC, BMS_E2E_ERR_DEC, BMS_E2E_ERR_LIMIT);
    debounce_init(&c->e2e_alive, BMS_E2E_ERR_INC, BMS_E2E_ERR_DEC, BMS_E2E_ERR_LIMIT);
    sensor_monitor_init(&c->mon_a);
    sensor_monitor_init(&c->mon_b);
    sensor_monitor_init(&c->mon_cool);
    plausibility_init(&c->plaus, BMS_SENS_PLAUS_MAX_C, BMS_SENS_PLAUS_DEBOUNCE);
    lowpass_init(&c->t_filter, BMS_TEMP_FILTER_TAU_S, DT_S);
    pi_init(&c->pi, BMS_PI_KP, BMS_PI_KI, DT_S, 0.0f, 1.0f);
    derate_init(&c->derate, DERATE_DEFAULT_MAP, DERATE_DEFAULT_MAP_LEN, BMS_DERATE_HYST_C);
    rate_limiter_init(&c->pump_rl, BMS_PUMP_RATE_PER_S, DT_S, 0.0f);
    rate_limiter_init(&c->chiller_rl, BMS_CHILLER_RATE_PER_S, DT_S, 0.0f);
    sm_init(&c->sm);
    c->out.state = BMS_INIT;
}

static void track_e2e(bms_core_t *c, e2e_status_t st)
{
    (void)debounce_step(&c->e2e_crc, st == E2E_ERR_CRC);
    (void)debounce_step(&c->e2e_alive, (st == E2E_ERR_REPEATED) || (st == E2E_ERR_SEQUENCE));
}

static void on_cell_temps(bms_core_t *c, const uint8_t data[8])
{
    const can_message_t *m = can_find_message(CAN_ID_CELL_TEMPS);
    const uint16_t raw_a = (uint16_t)can_signal_raw(&m->signals[SIG_T_CELL_A], data);
    const uint16_t raw_b = (uint16_t)can_signal_raw(&m->signals[SIG_T_CELL_B], data);
    const uint16_t raw_c = (uint16_t)can_signal_raw(&m->signals[SIG_T_COOL_IN], data);
    c->t_a = can_signal_get(&m->signals[SIG_T_CELL_A], data);
    c->t_b = can_signal_get(&m->signals[SIG_T_CELL_B], data);
    c->t_cool_in = can_signal_get(&m->signals[SIG_T_COOL_IN], data);

    c->st_a = sensor_monitor_step(&c->mon_a, raw_a, c->t_a);
    c->st_b = sensor_monitor_step(&c->mon_b, raw_b, c->t_b);
    c->st_cool = sensor_monitor_step(&c->mon_cool, raw_c, c->t_cool_in);
    c->st_cool.stuck = false; /* coolant temperature may legitimately sit still */

    const bool a_ok = !sensor_status_faulty(c->st_a);
    const bool b_ok = !sensor_status_faulty(c->st_b);
    if (a_ok && b_ok) {
        c->plaus_fault = plausibility_step(&c->plaus, c->t_a, c->t_b);
        c->t_fused = (c->t_a > c->t_b) ? c->t_a : c->t_b;
    } else if (a_ok) {
        c->t_fused = c->t_a;
    } else if (b_ok) {
        c->t_fused = c->t_b;
    } else {
        /* keep last fused value; the state machine goes to SAFE_STATE */
    }
    c->have_temps = true;
}

void bms_core_on_frame(bms_core_t *c, uint32_t can_id, const uint8_t data[8], uint32_t now_ms)
{
    if (can_id == CAN_ID_CELL_TEMPS) {
        const e2e_status_t st = e2e_rx_check(&c->rx_temps, can_id, data, now_ms);
        track_e2e(c, st);
        if (st == E2E_OK) {
            on_cell_temps(c, data);
        }
    } else if (can_id == CAN_ID_PACK_STATUS) {
        const e2e_status_t st = e2e_rx_check(&c->rx_pack, can_id, data, now_ms);
        track_e2e(c, st);
        if (st == E2E_OK) {
            const can_message_t *m = can_find_message(CAN_ID_PACK_STATUS);
            c->pack_current_a = can_signal_get(&m->signals[SIG_PACK_I], data);
            c->pack_soc = can_signal_get(&m->signals[SIG_PACK_SOC], data);
            c->have_pack = true;
        }
    } else {
        /* not consumed by the controller */
    }
}

void bms_core_report_watchdog_fault(bms_core_t *c)
{
    c->watchdog_fault = true;
}

static uint16_t collect_faults(const bms_core_t *c, uint32_t now_ms, float t_filt)
{
    uint16_t f = 0u;
    f |= c->st_a.range ? FAULT_SENS_A_RANGE : 0u;
    f |= c->st_a.stuck ? FAULT_SENS_A_STUCK : 0u;
    f |= c->st_a.roc ? FAULT_SENS_A_ROC : 0u;
    f |= c->st_b.range ? FAULT_SENS_B_RANGE : 0u;
    f |= c->st_b.stuck ? FAULT_SENS_B_STUCK : 0u;
    f |= c->st_b.roc ? FAULT_SENS_B_ROC : 0u;
    f |= c->plaus_fault ? FAULT_SENS_AB_PLAUS : 0u;
    f |= (c->st_cool.range || c->st_cool.roc) ? FAULT_SENS_COOLANT : 0u;
    f |= c->e2e_crc.active ? FAULT_E2E_CRC : 0u;
    f |= c->e2e_alive.active ? FAULT_E2E_ALIVE : 0u;
    f |= e2e_rx_timed_out(&c->rx_temps, now_ms) ? FAULT_CAN_TIMEOUT_TEMPS : 0u;
    f |= e2e_rx_timed_out(&c->rx_pack, now_ms) ? FAULT_CAN_TIMEOUT_PACK : 0u;
    f |= (c->have_temps && t_filt > BMS_T_OVERTEMP_C) ? FAULT_OVERTEMP : 0u;
    f |= c->watchdog_fault ? FAULT_WATCHDOG : 0u;
    return (uint16_t)f;
}

const bms_outputs_t *bms_core_step(bms_core_t *c, uint32_t now_ms)
{
    const float t_filt = c->have_temps ? lowpass_step(&c->t_filter, c->t_fused) : 0.0f;
    const uint16_t flags = collect_faults(c, now_ms, t_filt);

    sm_inputs_t in;
    in.inputs_valid = c->have_temps && c->have_pack && (flags == 0u);
    in.t_cell_c = t_filt;
    in.fault_flags = flags;
    const bms_state_t state = sm_step(&c->sm, &in);

    float pump_target = 0.0f;
    float chiller_target = 0.0f;
    float factor = 0.0f;
    float i_req = 0.0f;
    bool enable = false;

    if (state == BMS_INIT) {
        pi_reset(&c->pi);
    } else if (state == BMS_SAFE_STATE) {
        /* Safe state: stop charging, maximum cooling. */
        pump_target = 1.0f;
        chiller_target = 1.0f;
    } else {
        const float u = pi_step(&c->pi, t_filt - BMS_T_SETPOINT_C);
        pump_target = BMS_PUMP_MIN + (1.0f - BMS_PUMP_MIN) * clampf(2.0f * u, 0.0f, 1.0f);
        chiller_target = clampf((u - 0.25f) / 0.75f, 0.0f, 1.0f);
        factor = derate_step(&c->derate, t_filt);
        i_req = factor * BMS_I_CHARGE_MAX_A;
        if (state == BMS_DERATE && i_req > 0.5f * BMS_I_CHARGE_MAX_A && (c->sm.latched >= BMS_DERATE)) {
            i_req = 0.5f * BMS_I_CHARGE_MAX_A; /* sensor redundancy lost */
        }
        if (state == BMS_LIMP) {
            pump_target = 1.0f;
            i_req = (i_req > BMS_I_LIMP_A) ? BMS_I_LIMP_A : i_req;
        }
        enable = i_req > 0.0f;
    }

    c->out.pump = rate_limiter_step(&c->pump_rl, pump_target);
    c->out.chiller = rate_limiter_step(&c->chiller_rl, chiller_target);
    c->out.i_req_a = i_req;
    c->out.v_req_v = enable ? BMS_V_CHARGE_MAX_V : 0.0f;
    c->out.charge_enable = enable;
    c->out.derate_factor = i_req / BMS_I_CHARGE_MAX_A;
    c->out.state = state;
    c->out.latched = c->sm.latched > BMS_NORMAL;
    c->out.fault_flags = flags;
    c->out.t_cell_c = t_filt;
    return &c->out;
}

void bms_core_pack_thermal_cmd(const bms_core_t *c, uint8_t data[8], uint8_t counter)
{
    const can_message_t *m = can_find_message(CAN_ID_THERMAL_CMD);
    memset(data, 0, 8u);
    can_signal_set(&m->signals[SIG_PUMP], data, c->out.pump * 100.0f);
    can_signal_set(&m->signals[SIG_CHILLER], data, c->out.chiller * 100.0f);
    can_signal_set_raw(&m->signals[SIG_STATE], data, (int64_t)c->out.state);
    can_signal_set_raw(&m->signals[SIG_LATCHED], data, c->out.latched ? 1 : 0);
    can_signal_set_raw(&m->signals[SIG_FLAGS], data, (int64_t)c->out.fault_flags);
    e2e_protect(CAN_ID_THERMAL_CMD, data, counter);
}

void bms_core_pack_charge_request(const bms_core_t *c, uint8_t data[8], uint8_t counter)
{
    const can_message_t *m = can_find_message(CAN_ID_CHARGE_REQUEST);
    memset(data, 0, 8u);
    can_signal_set(&m->signals[SIG_I_REQ], data, c->out.i_req_a);
    can_signal_set(&m->signals[SIG_V_REQ], data, c->out.v_req_v);
    can_signal_set_raw(&m->signals[SIG_CHG_EN], data, c->out.charge_enable ? 1 : 0);
    can_signal_set(&m->signals[SIG_DERATE], data, c->out.derate_factor * 100.0f);
    e2e_protect(CAN_ID_CHARGE_REQUEST, data, counter);
}

void bms_core_pack_fault_status(const bms_core_t *c, uint8_t data[8], uint8_t counter, bool watchdog_ok)
{
    const can_message_t *m = can_find_message(CAN_ID_FAULT_STATUS);
    memset(data, 0, 8u);
    can_signal_set_raw(&m->signals[SIG_DIAG_FLAGS], data, (int64_t)c->sm.latched_flags);
    can_signal_set_raw(&m->signals[SIG_DIAG_STATE], data, (int64_t)c->out.state);
    can_signal_set_raw(&m->signals[SIG_WDG_OK], data, watchdog_ok ? 1 : 0);
    can_signal_set(&m->signals[SIG_DIAG_TMAX], data, c->out.t_cell_c);
    e2e_protect(CAN_ID_FAULT_STATUS, data, counter);
}

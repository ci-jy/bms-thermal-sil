#ifndef BMS_CORE_H
#define BMS_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "derate.h"
#include "e2e.h"
#include "fault_detect.h"
#include "filter.h"
#include "pi_ctrl.h"
#include "state_machine.h"

/* RTOS-independent controller: consumes received CAN frames, runs every
 * control period and produces the actuator commands, charge request and
 * safety state. All state lives in this struct (no dynamic allocation). */

typedef struct {
    float pump;             /* 0..1 */
    float chiller;          /* 0..1 */
    float i_req_a;
    float v_req_v;
    bool charge_enable;
    float derate_factor;    /* 0..1 */
    bms_state_t state;
    bool latched;
    uint16_t fault_flags;   /* active flags this cycle */
    float t_cell_c;         /* fused, filtered */
} bms_outputs_t;

typedef struct {
    /* Inputs */
    e2e_rx_t rx_temps;
    e2e_rx_t rx_pack;
    debounce_t e2e_crc;
    debounce_t e2e_alive;
    sensor_monitor_t mon_a;
    sensor_monitor_t mon_b;
    sensor_monitor_t mon_cool;
    plausibility_t plaus;
    sensor_status_t st_a;
    sensor_status_t st_b;
    sensor_status_t st_cool;
    bool plaus_fault;
    float t_a;
    float t_b;
    float t_cool_in;
    float pack_current_a;
    float pack_soc;
    float t_fused;
    bool have_temps;
    bool have_pack;
    bool watchdog_fault;
    /* Control */
    lowpass_t t_filter;
    pi_ctrl_t pi;
    derate_t derate;
    rate_limiter_t pump_rl;
    rate_limiter_t chiller_rl;
    state_machine_t sm;
    bms_outputs_t out;
} bms_core_t;

void bms_core_init(bms_core_t *c, uint32_t now_ms);
/* Feed one received frame (any ID; unknown IDs are ignored). */
void bms_core_on_frame(bms_core_t *c, uint32_t can_id, const uint8_t data[8], uint32_t now_ms);
/* Run one control period. */
const bms_outputs_t *bms_core_step(bms_core_t *c, uint32_t now_ms);
void bms_core_report_watchdog_fault(bms_core_t *c);

/* Build the transmitted frames from the current outputs (counter = alive). */
void bms_core_pack_thermal_cmd(const bms_core_t *c, uint8_t data[8], uint8_t counter);
void bms_core_pack_charge_request(const bms_core_t *c, uint8_t data[8], uint8_t counter);
void bms_core_pack_fault_status(const bms_core_t *c, uint8_t data[8], uint8_t counter, bool watchdog_ok);

#endif

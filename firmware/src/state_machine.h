#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdbool.h>
#include <stdint.h>

/* Safety states, ordered by severity. Values match the DBC BmsState signal. */
typedef enum {
    BMS_INIT = 0,
    BMS_NORMAL = 1,
    BMS_DERATE = 2,
    BMS_LIMP = 3,
    BMS_SAFE_STATE = 4,
} bms_state_t;

/* Fault flag bits, reported in FaultFlags / DiagFaultFlags. */
enum {
    FAULT_SENS_A_RANGE = 1u << 0,
    FAULT_SENS_A_STUCK = 1u << 1,
    FAULT_SENS_A_ROC = 1u << 2,
    FAULT_SENS_B_RANGE = 1u << 3,
    FAULT_SENS_B_STUCK = 1u << 4,
    FAULT_SENS_B_ROC = 1u << 5,
    FAULT_SENS_AB_PLAUS = 1u << 6,
    FAULT_SENS_COOLANT = 1u << 7,
    FAULT_E2E_CRC = 1u << 8,
    FAULT_E2E_ALIVE = 1u << 9,
    FAULT_CAN_TIMEOUT_TEMPS = 1u << 10,
    FAULT_CAN_TIMEOUT_PACK = 1u << 11,
    FAULT_OVERTEMP = 1u << 12,
    FAULT_WATCHDOG = 1u << 13,
};

#define FAULT_MASK_SENS_A (FAULT_SENS_A_RANGE | FAULT_SENS_A_STUCK | FAULT_SENS_A_ROC)
#define FAULT_MASK_SENS_B (FAULT_SENS_B_RANGE | FAULT_SENS_B_STUCK | FAULT_SENS_B_ROC)
/* Degraded but redundancy still covers it: reduce charge current. */
#define FAULT_CLASS_DERATE (FAULT_MASK_SENS_A | FAULT_MASK_SENS_B | FAULT_SENS_COOLANT)
/* Data integrity in question: conservative limp-home current. */
#define FAULT_CLASS_LIMP (FAULT_SENS_AB_PLAUS | FAULT_E2E_CRC | FAULT_E2E_ALIVE | FAULT_CAN_TIMEOUT_PACK)
/* Cell temperature no longer observable or already too high: stop charging. */
#define FAULT_CLASS_SAFE (FAULT_CAN_TIMEOUT_TEMPS | FAULT_OVERTEMP | FAULT_WATCHDOG)

#define SM_INIT_VALID_CYCLES 5u

typedef struct {
    bms_state_t state;
    bms_state_t latched;      /* worst fault reaction seen this drive cycle */
    bool thermal_derate;      /* hysteretic over-temperature derate request */
    uint16_t valid_cycles;
    uint16_t latched_flags;   /* union of all fault flags seen */
} state_machine_t;

typedef struct {
    bool inputs_valid;        /* fresh, E2E-valid frames on both inputs */
    float t_cell_c;           /* fused, filtered cell temperature */
    uint16_t fault_flags;     /* currently active fault flags */
} sm_inputs_t;

void sm_init(state_machine_t *sm);
/* Severity required by a set of fault flags (BMS_NORMAL when none). */
bms_state_t sm_fault_reaction(uint16_t flags);
bms_state_t sm_step(state_machine_t *sm, const sm_inputs_t *in);
const char *sm_state_name(bms_state_t s);

#endif

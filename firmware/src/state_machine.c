#include "state_machine.h"
#include "bms_config.h"

static bms_state_t max_state(bms_state_t a, bms_state_t b)
{
    return (a > b) ? a : b;
}

void sm_init(state_machine_t *sm)
{
    sm->state = BMS_INIT;
    sm->latched = BMS_NORMAL;
    sm->thermal_derate = false;
    sm->valid_cycles = 0u;
    sm->latched_flags = 0u;
}

bms_state_t sm_fault_reaction(uint16_t flags)
{
    const uint16_t both_sensors = FAULT_MASK_SENS_A | FAULT_MASK_SENS_B;
    if ((flags & FAULT_CLASS_SAFE) != 0u) {
        return BMS_SAFE_STATE;
    }
    if (((flags & FAULT_MASK_SENS_A) != 0u) && ((flags & FAULT_MASK_SENS_B) != 0u)) {
        return BMS_SAFE_STATE; /* no valid cell temperature left */
    }
    if ((flags & FAULT_CLASS_LIMP) != 0u) {
        return BMS_LIMP;
    }
    if ((flags & (both_sensors | FAULT_SENS_COOLANT)) != 0u) {
        return BMS_DERATE;
    }
    return BMS_NORMAL;
}

bms_state_t sm_step(state_machine_t *sm, const sm_inputs_t *in)
{
    /* Fault reactions latch: severity never decreases within a drive cycle. */
    sm->latched_flags |= in->fault_flags;
    sm->latched = max_state(sm->latched, sm_fault_reaction(in->fault_flags));

    if (sm->latched == BMS_SAFE_STATE) {
        sm->state = BMS_SAFE_STATE;
        return sm->state;
    }

    if (sm->state == BMS_INIT) {
        sm->valid_cycles = in->inputs_valid ? (uint16_t)(sm->valid_cycles + 1u) : 0u;
        if (sm->valid_cycles < SM_INIT_VALID_CYCLES) {
            return sm->state;
        }
    }

    /* Thermal derate is not a fault and recovers with hysteresis. */
    if (in->t_cell_c > BMS_T_DERATE_STATE_C) {
        sm->thermal_derate = true;
    } else if (in->t_cell_c < BMS_T_DERATE_EXIT_C) {
        sm->thermal_derate = false;
    } else {
        /* hold */
    }

    sm->state = max_state(sm->latched, sm->thermal_derate ? BMS_DERATE : BMS_NORMAL);
    return sm->state;
}

const char *sm_state_name(bms_state_t s)
{
    switch (s) {
    case BMS_INIT: return "INIT";
    case BMS_NORMAL: return "NORMAL";
    case BMS_DERATE: return "DERATE";
    case BMS_LIMP: return "LIMP";
    case BMS_SAFE_STATE: return "SAFE_STATE";
    default: return "UNKNOWN";
    }
}

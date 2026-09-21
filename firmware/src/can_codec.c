#include "can_codec.h"

static const can_signal_t SIGS_SYNC[] = {
    {"SIL_StepCounter", 0u, 32u, false, 1.0f, 0.0f},
};
static const can_signal_t SIGS_CELL_TEMPS[] = {
    {"T_CellA", 0u, 12u, false, 0.1f, -40.0f},
    {"T_CellB", 12u, 12u, false, 0.1f, -40.0f},
    {"T_CoolantIn", 24u, 12u, false, 0.1f, -40.0f},
    {"T_CoolantOut", 36u, 12u, false, 0.1f, -40.0f},
    {"CellTemps_Alive", 48u, 4u, false, 1.0f, 0.0f},
    {"CellTemps_CRC", 56u, 8u, false, 1.0f, 0.0f},
};
static const can_signal_t SIGS_PACK_STATUS[] = {
    {"PackCurrent", 0u, 16u, true, 0.1f, 0.0f},
    {"PackVoltage", 16u, 16u, false, 0.1f, 0.0f},
    {"PackSOC", 32u, 8u, false, 0.5f, 0.0f},
    {"PackStatus_Alive", 48u, 4u, false, 1.0f, 0.0f},
    {"PackStatus_CRC", 56u, 8u, false, 1.0f, 0.0f},
};
static const can_signal_t SIGS_THERMAL_CMD[] = {
    {"PumpDutyCmd", 0u, 8u, false, 0.5f, 0.0f},
    {"ChillerDutyCmd", 8u, 8u, false, 0.5f, 0.0f},
    {"BmsState", 16u, 3u, false, 1.0f, 0.0f},
    {"FaultLatched", 19u, 1u, false, 1.0f, 0.0f},
    {"FaultFlags", 20u, 16u, false, 1.0f, 0.0f},
    {"ThermalCmd_Alive", 48u, 4u, false, 1.0f, 0.0f},
    {"ThermalCmd_CRC", 56u, 8u, false, 1.0f, 0.0f},
};
static const can_signal_t SIGS_CHARGE_REQUEST[] = {
    {"ChargeCurrentReq", 0u, 16u, false, 0.1f, 0.0f},
    {"ChargeVoltageReq", 16u, 16u, false, 0.1f, 0.0f},
    {"ChargeEnable", 32u, 1u, false, 1.0f, 0.0f},
    {"DerateFactor", 40u, 8u, false, 0.5f, 0.0f},
    {"ChargeRequest_Alive", 48u, 4u, false, 1.0f, 0.0f},
    {"ChargeRequest_CRC", 56u, 8u, false, 1.0f, 0.0f},
};
static const can_signal_t SIGS_FAULT_STATUS[] = {
    {"DiagFaultFlags", 0u, 16u, false, 1.0f, 0.0f},
    {"DiagBmsState", 16u, 3u, false, 1.0f, 0.0f},
    {"WatchdogOk", 19u, 1u, false, 1.0f, 0.0f},
    {"DiagMaxCellTemp", 24u, 12u, false, 0.1f, -40.0f},
    {"FaultStatus_Alive", 48u, 4u, false, 1.0f, 0.0f},
    {"FaultStatus_CRC", 56u, 8u, false, 1.0f, 0.0f},
};

#define N_OF(a) (sizeof(a) / sizeof((a)[0]))

const can_message_t CAN_MESSAGES[] = {
    {CAN_ID_SIL_SYNC, "SIL_Sync", SIGS_SYNC, N_OF(SIGS_SYNC)},
    {CAN_ID_CELL_TEMPS, "PLANT_CellTemps", SIGS_CELL_TEMPS, N_OF(SIGS_CELL_TEMPS)},
    {CAN_ID_PACK_STATUS, "PLANT_PackStatus", SIGS_PACK_STATUS, N_OF(SIGS_PACK_STATUS)},
    {CAN_ID_THERMAL_CMD, "BMS_ThermalCmd", SIGS_THERMAL_CMD, N_OF(SIGS_THERMAL_CMD)},
    {CAN_ID_CHARGE_REQUEST, "BMS_ChargeRequest", SIGS_CHARGE_REQUEST, N_OF(SIGS_CHARGE_REQUEST)},
    {CAN_ID_FAULT_STATUS, "BMS_FaultStatus", SIGS_FAULT_STATUS, N_OF(SIGS_FAULT_STATUS)},
};
const size_t CAN_MESSAGES_LEN = N_OF(CAN_MESSAGES);

const can_message_t *can_find_message(uint32_t id)
{
    for (size_t i = 0u; i < CAN_MESSAGES_LEN; i++) {
        if (CAN_MESSAGES[i].id == id) {
            return &CAN_MESSAGES[i];
        }
    }
    return NULL;
}

static uint64_t load_le64(const uint8_t data[8])
{
    uint64_t v = 0u;
    for (int i = 7; i >= 0; i--) {
        v = (v << 8) | data[i];
    }
    return v;
}

static void store_le64(uint8_t data[8], uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        data[i] = (uint8_t)(v >> (8 * i));
    }
}

static uint64_t mask_of(uint8_t length)
{
    return (length >= 64u) ? ~(uint64_t)0u : (((uint64_t)1u << length) - 1u);
}

uint64_t can_get_raw(const uint8_t data[8], uint8_t start, uint8_t length)
{
    return (load_le64(data) >> start) & mask_of(length);
}

void can_set_raw(uint8_t data[8], uint8_t start, uint8_t length, uint64_t raw)
{
    const uint64_t m = mask_of(length) << start;
    const uint64_t v = (load_le64(data) & ~m) | ((raw << start) & m);
    store_le64(data, v);
}

int64_t can_signal_raw(const can_signal_t *s, const uint8_t data[8])
{
    const uint64_t raw = can_get_raw(data, s->start, s->length);
    if (s->is_signed && s->length < 64u && ((raw >> (s->length - 1u)) & 1u)) {
        return (int64_t)(raw | ~mask_of(s->length));
    }
    return (int64_t)raw;
}

float can_signal_get(const can_signal_t *s, const uint8_t data[8])
{
    return (float)can_signal_raw(s, data) * s->scale + s->offset;
}

void can_signal_set_raw(const can_signal_t *s, uint8_t data[8], int64_t raw)
{
    can_set_raw(data, s->start, s->length, (uint64_t)raw);
}

void can_signal_set(const can_signal_t *s, uint8_t data[8], float value)
{
    const double r = ((double)value - (double)s->offset) / (double)s->scale;
    int64_t raw = (int64_t)((r >= 0.0) ? (r + 0.5) : (r - 0.5));
    int64_t lo = 0;
    int64_t hi = (int64_t)mask_of(s->length);
    if (s->is_signed) {
        hi = (int64_t)(mask_of(s->length) >> 1);
        lo = -hi - 1;
    }
    if (raw < lo) {
        raw = lo;
    } else if (raw > hi) {
        raw = hi;
    } else {
        /* in range */
    }
    can_signal_set_raw(s, data, raw);
}

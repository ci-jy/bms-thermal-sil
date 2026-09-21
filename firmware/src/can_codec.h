#ifndef CAN_CODEC_H
#define CAN_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Table-driven pack/unpack for the messages in dbc/bms_thermal.dbc.
 * All signals are little-endian (Intel, @1) as in the DBC. */

typedef struct {
    const char *name;
    uint8_t start;
    uint8_t length;
    bool is_signed;
    float scale;
    float offset;
} can_signal_t;

typedef struct {
    uint32_t id;
    const char *name;
    const can_signal_t *signals;
    size_t n_signals;
} can_message_t;

#define CAN_ID_SIL_SYNC          0x050u
#define CAN_ID_CELL_TEMPS        0x100u
#define CAN_ID_PACK_STATUS       0x110u
#define CAN_ID_THERMAL_CMD       0x200u
#define CAN_ID_CHARGE_REQUEST    0x210u
#define CAN_ID_FAULT_STATUS      0x300u

/* Signal indexes inside each message table (same order as the DBC). */
enum { SIG_SYNC_STEP = 0 };
enum { SIG_T_CELL_A = 0, SIG_T_CELL_B, SIG_T_COOL_IN, SIG_T_COOL_OUT, SIG_TEMPS_ALIVE, SIG_TEMPS_CRC };
enum { SIG_PACK_I = 0, SIG_PACK_V, SIG_PACK_SOC, SIG_PACK_ALIVE, SIG_PACK_CRC };
enum { SIG_PUMP = 0, SIG_CHILLER, SIG_STATE, SIG_LATCHED, SIG_FLAGS, SIG_CMD_ALIVE, SIG_CMD_CRC };
enum { SIG_I_REQ = 0, SIG_V_REQ, SIG_CHG_EN, SIG_DERATE, SIG_REQ_ALIVE, SIG_REQ_CRC };
enum { SIG_DIAG_FLAGS = 0, SIG_DIAG_STATE, SIG_WDG_OK, SIG_DIAG_TMAX, SIG_DIAG_ALIVE, SIG_DIAG_CRC };

extern const can_message_t CAN_MESSAGES[];
extern const size_t CAN_MESSAGES_LEN;

const can_message_t *can_find_message(uint32_t id);

uint64_t can_get_raw(const uint8_t data[8], uint8_t start, uint8_t length);
void can_set_raw(uint8_t data[8], uint8_t start, uint8_t length, uint64_t raw);

/* Sign-extended raw value of a signal. */
int64_t can_signal_raw(const can_signal_t *s, const uint8_t data[8]);
float can_signal_get(const can_signal_t *s, const uint8_t data[8]);
/* Physical value -> raw with rounding; saturates to the representable range.
 * Takes a double so 32-bit counters keep full precision. */
void can_signal_set(const can_signal_t *s, uint8_t data[8], double value);
void can_signal_set_raw(const can_signal_t *s, uint8_t data[8], int64_t raw);

#endif

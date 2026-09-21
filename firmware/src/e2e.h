#ifndef E2E_H
#define E2E_H

#include <stdbool.h>
#include <stdint.h>

/* End-to-end protection for 8-byte CAN frames in the style of AUTOSAR E2E
 * profile 1: a 4-bit alive counter in bits 48..51 and a CRC-8 (SAE J1850)
 * in byte 7, computed over the low byte of the CAN ID followed by bytes 0..6. */

#define E2E_ALIVE_BYTE 6u
#define E2E_CRC_BYTE   7u

typedef enum {
    E2E_OK = 0,
    E2E_ERR_CRC,
    E2E_ERR_REPEATED,  /* alive counter did not change */
    E2E_ERR_SEQUENCE,  /* alive counter jumped by more than the allowed delta */
} e2e_status_t;

typedef struct {
    uint8_t last_counter;
    bool have_counter;
    uint32_t last_ok_ms;
    bool ever_ok;
    uint32_t timeout_ms;
    uint8_t max_delta;
} e2e_rx_t;

uint8_t crc8_j1850(const uint8_t *data, uint32_t len, uint8_t init);
uint8_t e2e_crc(uint32_t can_id, const uint8_t data[8]);
/* Write alive counter and CRC into data (sender side). */
void e2e_protect(uint32_t can_id, uint8_t data[8], uint8_t counter);

void e2e_rx_init(e2e_rx_t *rx, uint32_t timeout_ms, uint8_t max_delta, uint32_t now_ms);
e2e_status_t e2e_rx_check(e2e_rx_t *rx, uint32_t can_id, const uint8_t data[8], uint32_t now_ms);
/* True when no valid frame was accepted for longer than the timeout. */
bool e2e_rx_timed_out(const e2e_rx_t *rx, uint32_t now_ms);

#endif

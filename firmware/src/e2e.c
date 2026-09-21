#include "e2e.h"

uint8_t crc8_j1850(const uint8_t *data, uint32_t len, uint8_t init)
{
    uint8_t crc = init;
    for (uint32_t i = 0u; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0u; b < 8u; b++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x1Du) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

uint8_t e2e_crc(uint32_t can_id, const uint8_t data[8])
{
    const uint8_t id = (uint8_t)(can_id & 0xFFu);
    uint8_t crc = crc8_j1850(&id, 1u, 0xFFu);
    crc = crc8_j1850(data, 7u, crc);
    return (uint8_t)(crc ^ 0xFFu);
}

void e2e_protect(uint32_t can_id, uint8_t data[8], uint8_t counter)
{
    data[E2E_ALIVE_BYTE] = (uint8_t)((data[E2E_ALIVE_BYTE] & 0xF0u) | (counter & 0x0Fu));
    data[E2E_CRC_BYTE] = e2e_crc(can_id, data);
}

void e2e_rx_init(e2e_rx_t *rx, uint32_t timeout_ms, uint8_t max_delta, uint32_t now_ms)
{
    rx->last_counter = 0u;
    rx->have_counter = false;
    rx->last_ok_ms = now_ms;
    rx->ever_ok = false;
    rx->timeout_ms = timeout_ms;
    rx->max_delta = max_delta;
}

e2e_status_t e2e_rx_check(e2e_rx_t *rx, uint32_t can_id, const uint8_t data[8], uint32_t now_ms)
{
    if (e2e_crc(can_id, data) != data[E2E_CRC_BYTE]) {
        return E2E_ERR_CRC;
    }
    const uint8_t counter = data[E2E_ALIVE_BYTE] & 0x0Fu;
    e2e_status_t st = E2E_OK;
    if (rx->have_counter) {
        const uint8_t delta = (uint8_t)((counter - rx->last_counter) & 0x0Fu);
        if (delta == 0u) {
            st = E2E_ERR_REPEATED;
        } else if (delta > rx->max_delta) {
            st = E2E_ERR_SEQUENCE;
        } else {
            st = E2E_OK;
        }
    }
    if (st != E2E_ERR_REPEATED) {
        /* Resynchronise on sequence errors so a single gap does not cascade. */
        rx->last_counter = counter;
        rx->have_counter = true;
    }
    if (st == E2E_OK) {
        rx->last_ok_ms = now_ms;
        rx->ever_ok = true;
    }
    return st;
}

bool e2e_rx_timed_out(const e2e_rx_t *rx, uint32_t now_ms)
{
    return (uint32_t)(now_ms - rx->last_ok_ms) > rx->timeout_ms;
}

/* FreeRTOS application: 10 ms control task, CAN RX/TX task and 100 ms
 * diagnostics task with a watchdog check, exchanging frames with the plant.
 *
 * Usage: bms_app (--can IFACE | --fd N) [--realtime]
 *
 * Lockstep mode (default): the plant sends one SIL_Sync frame per 10 ms
 * step; the control task runs one cycle per SIL_Sync, so simulated time
 * is deterministic and independent of host load.
 * Real-time mode: the control task is released by vTaskDelayUntil every
 * 10 ms on the FreeRTOS tick and SIL_Sync frames are ignored. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "bms_config.h"
#include "bms_core.h"
#include "can_codec.h"
#include "transport.h"

#define STACK_WORDS 4096u
#define RX_QUEUE_LEN 32u
#define TX_QUEUE_LEN 16u

#define PRIO_CAN     (tskIDLE_PRIORITY + 1)
#define PRIO_CONTROL (tskIDLE_PRIORITY + 3)
#define PRIO_DIAG    (tskIDLE_PRIORITY + 4)

static transport_t g_transport;
static bool g_realtime;
static bms_core_t g_core;
static SemaphoreHandle_t g_core_lock;
static QueueHandle_t g_rx_queue;
static QueueHandle_t g_tx_queue;
static TaskHandle_t g_diag_task;
static volatile uint32_t g_control_runs;
static volatile uint32_t g_can_loops;
static uint32_t g_next_ms; /* lockstep: time of the step the next frames belong to */

static StaticTask_t s_tcb_can, s_tcb_control, s_tcb_diag, s_tcb_idle;
static StackType_t s_stack_can[STACK_WORDS], s_stack_control[STACK_WORDS];
static StackType_t s_stack_diag[STACK_WORDS], s_stack_idle[STACK_WORDS];
static StaticQueue_t s_rx_q, s_tx_q;
static uint8_t s_rx_storage[RX_QUEUE_LEN * sizeof(struct can_frame)];
static uint8_t s_tx_storage[TX_QUEUE_LEN * sizeof(struct can_frame)];
static StaticSemaphore_t s_lock;

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, configSTACK_DEPTH_TYPE *size)
{
    *tcb = &s_tcb_idle;
    *stack = s_stack_idle;
    *size = STACK_WORDS;
}

void vAssertCalled(const char *file, unsigned long line)
{
    fprintf(stderr, "ASSERT %s:%lu\n", file, line);
    abort();
}

static void send_frame(uint32_t id, const uint8_t data[8])
{
    struct can_frame f;
    memset(&f, 0, sizeof(f));
    f.can_id = id;
    f.can_dlc = 8;
    memcpy(f.data, data, 8);
    (void)xQueueSend(g_tx_queue, &f, portMAX_DELAY);
}

static void can_task(void *arg)
{
    (void)arg;
    struct can_frame f;
    for (;;) {
        int r;
        while ((r = transport_try_read(&g_transport, &f)) == 1) {
            if (f.can_id == CAN_ID_SIL_SYNC && g_realtime) {
                continue;
            }
            (void)xQueueSend(g_rx_queue, &f, portMAX_DELAY);
        }
        if (r < 0) {
            _exit(0); /* plant closed the bus: end of scenario */
        }
        while (xQueueReceive(g_tx_queue, &f, 0) == pdTRUE) {
            if (!transport_write(&g_transport, &f)) {
                _exit(2);
            }
        }
        g_can_loops++;
        taskYIELD();
    }
}

static void control_cycle(uint32_t now_ms)
{
    static uint8_t counter;
    uint8_t cmd[8];
    uint8_t req[8];

    xSemaphoreTake(g_core_lock, portMAX_DELAY);
    (void)bms_core_step(&g_core, now_ms);
    bms_core_pack_charge_request(&g_core, req, counter);
    bms_core_pack_thermal_cmd(&g_core, cmd, counter);
    xSemaphoreGive(g_core_lock);
    counter = (uint8_t)((counter + 1u) & 0x0Fu);
    g_control_runs++;

    /* In lockstep the diagnostics task runs every 10th cycle, before the
     * ThermalCmd frame that marks the end of the step is queued. */
    if (!g_realtime && (now_ms % BMS_DIAG_PERIOD_MS) == 0u) {
        xTaskNotifyGive(g_diag_task);
    }
    send_frame(CAN_ID_CHARGE_REQUEST, req);
    send_frame(CAN_ID_THERMAL_CMD, cmd);
}

static void consume_frame(const struct can_frame *f, uint32_t now_ms)
{
    xSemaphoreTake(g_core_lock, portMAX_DELAY);
    bms_core_on_frame(&g_core, f->can_id, f->data, now_ms);
    xSemaphoreGive(g_core_lock);
}

static void control_task(void *arg)
{
    (void)arg;
    struct can_frame f;
    if (g_realtime) {
        TickType_t wake = xTaskGetTickCount();
        for (;;) {
            vTaskDelayUntil(&wake, pdMS_TO_TICKS(BMS_CONTROL_PERIOD_MS));
            const uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            while (xQueueReceive(g_rx_queue, &f, 0) == pdTRUE) {
                consume_frame(&f, now);
            }
            control_cycle(now);
        }
    }
    for (;;) {
        (void)xQueueReceive(g_rx_queue, &f, portMAX_DELAY);
        if (f.can_id == CAN_ID_SIL_SYNC) {
            const can_message_t *m = can_find_message(CAN_ID_SIL_SYNC);
            const uint32_t now = (uint32_t)can_signal_raw(&m->signals[SIG_SYNC_STEP], f.data) * BMS_CONTROL_PERIOD_MS;
            g_next_ms = now + BMS_CONTROL_PERIOD_MS;
            control_cycle(now);
        } else {
            /* Frames sent before step k's SIL_Sync belong to step k. */
            consume_frame(&f, g_next_ms);
        }
    }
}

static void diag_task(void *arg)
{
    (void)arg;
    static uint8_t counter;
    uint32_t last_control = 0u;
    uint32_t last_can = 0u;
    bool baseline = false;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        if (g_realtime) {
            vTaskDelayUntil(&wake, pdMS_TO_TICKS(BMS_DIAG_PERIOD_MS));
        } else {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
        const uint32_t control_runs = g_control_runs;
        const uint32_t can_loops = g_can_loops;
        /* Watchdog: both tasks must have made progress since the last check. */
        const bool wdg_ok = !baseline ||
            (((control_runs - last_control) >= BMS_WDG_MIN_CONTROL_RUNS) && (can_loops != last_can));
        baseline = true;
        last_control = control_runs;
        last_can = can_loops;

        uint8_t data[8];
        xSemaphoreTake(g_core_lock, portMAX_DELAY);
        if (!wdg_ok) {
            bms_core_report_watchdog_fault(&g_core);
        }
        bms_core_pack_fault_status(&g_core, data, counter, wdg_ok);
        xSemaphoreGive(g_core_lock);
        counter = (uint8_t)((counter + 1u) & 0x0Fu);
        send_frame(CAN_ID_FAULT_STATUS, data);
    }
}

static void usage(void)
{
    fprintf(stderr, "usage: bms_app (--can IFACE | --fd N) [--realtime]\n");
    exit(1);
}

int main(int argc, char **argv)
{
    bool have_transport = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--can") == 0 && i + 1 < argc) {
            if (!transport_open_socketcan(&g_transport, argv[++i])) {
                perror("socketcan");
                return 1;
            }
            have_transport = true;
        } else if (strcmp(argv[i], "--fd") == 0 && i + 1 < argc) {
            transport_from_fd(&g_transport, atoi(argv[++i]));
            have_transport = true;
        } else if (strcmp(argv[i], "--realtime") == 0) {
            g_realtime = true;
        } else {
            usage();
        }
    }
    if (!have_transport) {
        usage();
    }

    bms_core_init(&g_core, 0u);
    g_core_lock = xSemaphoreCreateMutexStatic(&s_lock);
    g_rx_queue = xQueueCreateStatic(RX_QUEUE_LEN, sizeof(struct can_frame), s_rx_storage, &s_rx_q);
    g_tx_queue = xQueueCreateStatic(TX_QUEUE_LEN, sizeof(struct can_frame), s_tx_storage, &s_tx_q);
    (void)xTaskCreateStatic(can_task, "can", STACK_WORDS, NULL, PRIO_CAN, s_stack_can, &s_tcb_can);
    (void)xTaskCreateStatic(control_task, "ctrl", STACK_WORDS, NULL, PRIO_CONTROL, s_stack_control, &s_tcb_control);
    g_diag_task = xTaskCreateStatic(diag_task, "diag", STACK_WORDS, NULL, PRIO_DIAG, s_stack_diag, &s_tcb_diag);
    vTaskStartScheduler();
    return 0;
}

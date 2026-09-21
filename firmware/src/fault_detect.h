#ifndef FAULT_DETECT_H
#define FAULT_DETECT_H

#include <stdbool.h>
#include <stdint.h>

/* Up/down debounce counter: +inc on a failing sample, -dec on a passing one.
 * The fault qualifies when the counter reaches limit and dequalifies at 0. */
typedef struct {
    uint16_t count;
    uint16_t inc;
    uint16_t dec;
    uint16_t limit;
    bool active;
} debounce_t;

void debounce_init(debounce_t *d, uint16_t inc, uint16_t dec, uint16_t limit);
bool debounce_step(debounce_t *d, bool failing);

/* Per-channel checks on one temperature sensor. */
typedef struct {
    debounce_t range;
    debounce_t roc;
    uint16_t raw_prev;
    uint16_t same_count;
    uint16_t stuck_samples;
    float last_c;
    bool have_prev;
    bool stuck;
    bool sample_bad;   /* this sample failed range or rate check (undebounced) */
} sensor_monitor_t;

typedef struct {
    bool range;
    bool stuck;
    bool roc;
} sensor_status_t;

void sensor_monitor_init(sensor_monitor_t *m);
/* raw is the undecoded ADC/CAN value used for stuck-at detection; value_c is physical. */
sensor_status_t sensor_monitor_step(sensor_monitor_t *m, uint16_t raw, float value_c);
bool sensor_status_faulty(sensor_status_t s);
/* Usable for fusion: not qualified faulty and the latest sample passed the raw checks. */
bool sensor_monitor_usable(const sensor_monitor_t *m, sensor_status_t s);

/* Redundant-sensor plausibility: |a - b| above threshold, debounced. */
typedef struct {
    debounce_t deb;
    float max_diff;
} plausibility_t;

void plausibility_init(plausibility_t *p, float max_diff, uint16_t debounce);
bool plausibility_step(plausibility_t *p, float a, float b);

#endif

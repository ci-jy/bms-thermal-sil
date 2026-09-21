#ifndef FILTER_H
#define FILTER_H

#include <stdbool.h>

/* First-order low-pass filter: y += dt/(tau+dt) * (x - y). */
typedef struct {
    float alpha;
    float y;
    bool primed;
} lowpass_t;

void lowpass_init(lowpass_t *f, float tau_s, float dt_s);
float lowpass_step(lowpass_t *f, float x);
void lowpass_reset(lowpass_t *f, float y0);

/* Slew-rate limiter with a fixed maximum change per step. */
typedef struct {
    float max_step;
    float y;
} rate_limiter_t;

void rate_limiter_init(rate_limiter_t *r, float rate_per_s, float dt_s, float y0);
float rate_limiter_step(rate_limiter_t *r, float target);

float clampf(float x, float lo, float hi);

#endif

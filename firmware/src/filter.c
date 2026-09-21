#include "filter.h"

float clampf(float x, float lo, float hi)
{
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

void lowpass_init(lowpass_t *f, float tau_s, float dt_s)
{
    f->alpha = (tau_s <= 0.0f) ? 1.0f : dt_s / (tau_s + dt_s);
    f->y = 0.0f;
    f->primed = false;
}

void lowpass_reset(lowpass_t *f, float y0)
{
    f->y = y0;
    f->primed = true;
}

float lowpass_step(lowpass_t *f, float x)
{
    if (!f->primed) {
        lowpass_reset(f, x);
    } else {
        f->y += f->alpha * (x - f->y);
    }
    return f->y;
}

void rate_limiter_init(rate_limiter_t *r, float rate_per_s, float dt_s, float y0)
{
    r->max_step = rate_per_s * dt_s;
    r->y = y0;
}

float rate_limiter_step(rate_limiter_t *r, float target)
{
    r->y += clampf(target - r->y, -r->max_step, r->max_step);
    return r->y;
}

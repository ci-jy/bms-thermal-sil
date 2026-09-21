#include "pi_ctrl.h"
#include "filter.h"

void pi_init(pi_ctrl_t *pi, float kp, float ki, float dt, float out_min, float out_max)
{
    pi->kp = kp;
    pi->ki = ki;
    pi->dt = dt;
    pi->out_min = out_min;
    pi->out_max = out_max;
    pi_reset(pi);
}

void pi_reset(pi_ctrl_t *pi)
{
    pi->integ = 0.0f;
    pi->saturated = false;
}

float pi_step(pi_ctrl_t *pi, float error)
{
    const float p = pi->kp * error;
    const float candidate = pi->integ + pi->ki * error * pi->dt;
    const float unclamped = p + candidate;

    const bool high = unclamped > pi->out_max;
    const bool low = unclamped < pi->out_min;
    /* Conditional integration: freeze when saturated and error pushes further out. */
    if (!((high && error > 0.0f) || (low && error < 0.0f))) {
        pi->integ = clampf(candidate, pi->out_min, pi->out_max);
    }

    const float out = p + pi->integ;
    pi->saturated = (out > pi->out_max) || (out < pi->out_min);
    return clampf(out, pi->out_min, pi->out_max);
}

#ifndef PI_CTRL_H
#define PI_CTRL_H

#include <stdbool.h>

/* PI controller with output clamping and conditional-integration anti-windup.
 * The integrator only accumulates when doing so does not drive a saturated
 * output further into saturation, and is itself bounded to [out_min, out_max]. */
typedef struct {
    float kp;
    float ki;
    float dt;
    float out_min;
    float out_max;
    float integ;
    bool saturated;
} pi_ctrl_t;

void pi_init(pi_ctrl_t *pi, float kp, float ki, float dt, float out_min, float out_max);
void pi_reset(pi_ctrl_t *pi);
/* error = measurement - setpoint for a cooling loop (positive -> more cooling). */
float pi_step(pi_ctrl_t *pi, float error);

#endif

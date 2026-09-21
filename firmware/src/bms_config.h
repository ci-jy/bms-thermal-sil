/* Calibration constants for the thermal controller and safety monitors.
 * All limits are compile-time constants; nothing here is allocated at runtime. */
#ifndef BMS_CONFIG_H
#define BMS_CONFIG_H

#define BMS_CONTROL_PERIOD_MS     10u    /* control task period */
#define BMS_DIAG_PERIOD_MS        100u   /* diagnostics task period */

/* Temperature control */
#define BMS_T_SETPOINT_C          35.0f  /* cell temperature target during fast charge */
#define BMS_PI_KP                 0.30f  /* cooling demand per degC */
#define BMS_PI_KI                 0.004f /* cooling demand per degC*s */
#define BMS_PUMP_MIN              0.20f  /* minimum pump duty while active */
#define BMS_PUMP_RATE_PER_S       0.50f  /* pump duty slew limit (1/s) */
#define BMS_CHILLER_RATE_PER_S    0.20f  /* chiller duty slew limit (1/s) */
#define BMS_TEMP_FILTER_TAU_S     0.10f  /* low-pass on the fused cell temperature */

/* Charge current derating */
#define BMS_I_CHARGE_MAX_A        350.0f
#define BMS_I_LIMP_A              50.0f
#define BMS_V_CHARGE_MAX_V        403.2f /* 96s x 4.2 V */
#define BMS_DERATE_HYST_C         2.0f
#define BMS_T_DERATE_STATE_C      45.0f  /* enter DERATE state above this filtered temperature */
#define BMS_T_DERATE_EXIT_C       43.0f  /* leave DERATE state below this */
#define BMS_T_OVERTEMP_C          58.0f  /* hard limit -> SAFE_STATE */

/* Sensor checks */
#define BMS_SENS_MIN_C            (-30.0f)
#define BMS_SENS_MAX_C            120.0f
#define BMS_SENS_RANGE_DEBOUNCE   3u     /* samples */
#define BMS_SENS_STUCK_SAMPLES    15u    /* identical raw samples in a row */
#define BMS_SENS_ROC_MAX_C        10.0f  /* max change per 10 ms sample */
#define BMS_SENS_ROC_DEBOUNCE     2u
#define BMS_SENS_PLAUS_MAX_C      4.0f   /* max |A - B| */
#define BMS_SENS_PLAUS_DEBOUNCE   5u

/* CAN end-to-end protection */
#define BMS_E2E_MAX_DELTA         2u     /* accepted alive counter step (1 = no loss) */
#define BMS_E2E_TIMEOUT_MS        100u
#define BMS_E2E_ERR_INC           2u     /* debounce: +2 per bad frame */
#define BMS_E2E_ERR_DEC           1u     /* debounce: -1 per good frame */
#define BMS_E2E_ERR_LIMIT         6u

/* Watchdog: minimum task activations expected per diagnostics period */
#define BMS_WDG_MIN_CONTROL_RUNS  5u

/* Fault-tolerant time interval used by the SIL acceptance checks */
#define BMS_FTTI_MS               200u

#endif

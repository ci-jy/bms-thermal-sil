# Safety goals, requirements and traceability

This is a short safety concept written in the spirit of ISO 26262 (hazard →
safety goal → requirement → verification). It is not a compliance claim: no
HARA, ASIL decomposition or confirmation reviews were performed.

## Item and hazards

Item: thermal management and charge-current control of a ~65 kWh, 96s
traction battery during DC fast charge. Relevant hazard: **cell
over-temperature during charging**, which accelerates degradation and, in the
limit, can lead to thermal runaway.

| ID | Safety goal | Safe state | FTTI |
|---|---|---|---|
| SG-1 | Keep cell temperature below the charging limit (50 °C) during fast charge. | Charge current reduced (DERATE) | thermal, minutes |
| SG-2 | Detect corrupted, stale or implausible cell-temperature information and react before it is used to request current. | DERATE / LIMP | 200 ms |
| SG-3 | Stop charging when cell temperature can no longer be observed or exceeds the hard limit (58 °C). | SAFE_STATE: 0 A request, charge disabled, maximum cooling, latched | 200 ms |

The 200 ms FTTI is conservative: with the plant's heat capacity, worst-case
I²R heating raises cell temperature by well under 0.1 °C in 200 ms.

## State machine

```
INIT --5 valid cycles--> NORMAL <--thermal hysteresis 45/43 °C--> DERATE
  any state --LIMP-class fault--> LIMP --SAFE-class fault--> SAFE_STATE (latched)
```

Fault reactions latch for the drive cycle (severity never decreases). Thermal
derate is not a fault and recovers with hysteresis.

| Fault class | Flags | Reaction |
|---|---|---|
| Single redundant sensor lost | `SENS_A_*`, `SENS_B_*`, `SENS_COOLANT` | DERATE, current ≤ 50 % |
| Data integrity in doubt | `SENS_AB_PLAUS`, `E2E_CRC`, `E2E_ALIVE`, `CAN_TIMEOUT_PACK` | LIMP, current ≤ 50 A, pump 100 % |
| Cell temperature unobservable / too hot | both cell sensors, `CAN_TIMEOUT_TEMPS`, `OVERTEMP`, `WATCHDOG` | SAFE_STATE |

## Requirements and verification

Unit tests live in `firmware/tests/` (Unity), SIL tests in `sil/` (pytest).

| Req ID | Requirement | Goal | Verified by |
|---|---|---|---|
| REQ-THM-01 | Peak cell temperature stays below 50 °C in fast charge at 25/35/45 °C ambient. | SG-1 | `sil/test_closed_loop.py::test_req_thm_01_peak_cell_temperature_below_limit` |
| REQ-THM-02 | Overshoot above max(setpoint, initial temperature) ≤ 2 °C. | SG-1 | `sil/test_closed_loop.py::test_req_thm_02_overshoot_bounded` |
| REQ-THM-03 | Cell temperature enters and stays within 35 ± 1.5 °C within 1500 s while charging. | SG-1 | `sil/test_closed_loop.py::test_req_thm_03_settles_into_band`, `::test_req_thm_03_holds_setpoint_band_once_settled` |
| REQ-THM-04 | PI cooling controller has bounded outputs, anti-windup, and slew-limited pump (50 %/s) and chiller (20 %/s) commands. | SG-1 | `firmware/tests/test_filter_pi.c` (all), `sil/test_closed_loop.py::test_req_thm_04_actuators_bounded_and_rate_limited` |
| REQ-CHG-01 | Charge current is derated from a temperature map with 2 °C hysteresis. | SG-1 | `firmware/tests/test_derate.c` (all), `sil/test_closed_loop.py::test_req_chg_01_hot_pack_is_derated` |
| REQ-CHG-02 | A 10→80 % fast charge completes within 40 min at every tested ambient. | — (performance) | `sil/test_closed_loop.py::test_req_chg_02_charge_completes_in_time` |
| REQ-FD-01 | Out-of-range cell sensor (< −30 °C or > 120 °C) is detected after a 3-sample debounce; the bad sample is never used. | SG-2 | `test_fault_detect.c::test_range_fault_after_debounce`, `test_bms_core.c::test_sensor_fault_derates_on_redundant_channel`, SIL `sensor_a_open`, `sensor_a_short` |
| REQ-FD-02 | A sensor stuck at one raw value for 15 samples is detected. | SG-2 | `test_fault_detect.c::test_stuck_at_detected_after_n_identical_samples`, `::test_noisy_signal_never_stuck`, SIL `sensor_a_stuck` |
| REQ-FD-03 | A sensor changing by > 10 °C per sample (2-sample debounce) is detected. | SG-2 | `test_fault_detect.c::test_rate_of_change_fault`, SIL `sensor_a_spike` |
| REQ-FD-04 | Redundant sensors disagreeing by > 4 °C (5-sample debounce) cause LIMP with current ≤ 50 A. | SG-2 | `test_fault_detect.c::test_plausibility`, `test_bms_core.c::test_implausible_sensors_limp`, `sil/test_faults.py::test_req_fd_04_limp_caps_current`, SIL `sensor_ab_implausible` |
| REQ-E2E-01 | Frames carry CRC-8 SAE J1850 over data ID + payload; CRC errors are debounced per message (+2/−1, limit 6). | SG-2 | `test_e2e.c::test_crc8_sae_j1850_check_value`, `::test_crc_error_detected`, `test_bms_core.c::test_message_timeout_and_e2e_errors`, `sil/test_dbc_codec.py::test_req_e2e_01_crc_matches_python_reference`, SIL `crc_corrupt` |
| REQ-E2E-02 | Repeated or skipped (> 2) alive counters are rejected and debounced. | SG-2 | `test_e2e.c::test_repeated_and_sequence_errors`, `test_bms_core.c::test_alive_counter_error_on_pack_status`, SIL `alive_frozen` |
| REQ-E2E-03 | No valid cell-temperature frame for > 100 ms → SAFE_STATE; pack-status timeout → LIMP. | SG-3 | `test_e2e.c::test_timeout`, `test_bms_core.c::test_message_timeout_and_e2e_errors`, SIL `temps_frame_drop`, `pack_frame_drop` |
| REQ-SM-01 | State machine INIT → NORMAL → DERATE → LIMP → SAFE_STATE; fault reactions latch. | SG-2, SG-3 | `firmware/tests/test_state_machine.c` (all), `sil/test_faults.py::test_req_sm_01_fault_reaction_latches`, `sil/test_closed_loop.py::test_req_sm_01_init_then_normal` |
| REQ-SM-02 | SAFE_STATE requests 0 A, disables charging and commands maximum cooling. | SG-3 | `test_bms_core.c::test_both_sensors_lost_goes_safe`, `::test_watchdog_fault_goes_safe_and_frames_pack`, `::test_overtemperature_safe_state`, `sil/test_faults.py::test_req_sm_02_safe_state_stops_charging` |
| REQ-SAF-01 | Every injected fault reaches its reaction state within the 200 ms FTTI. | SG-2, SG-3 | `sil/test_faults.py::test_req_saf_01_fault_reaction_within_ftti` (10 fault kinds) |
| REQ-SAF-02 | No fault flag is set and no fault reaction occurs in fault-free charges. | SG-1 | `sil/test_closed_loop.py::test_req_saf_02_no_false_trips` |
| REQ-SAF-03 | A diagnostics-task watchdog detects a stalled control or CAN task and forces SAFE_STATE. | SG-3 | `test_bms_core.c::test_watchdog_fault_goes_safe_and_frames_pack` (reaction only; see Limitations) |
| REQ-CAN-01 | C pack/unpack matches cantools for every DBC signal (raw, physical, edge values). | SG-2 | `sil/test_dbc_codec.py` (all), `firmware/tests/test_can_codec.c` (all) |
| REQ-CAN-02 | The SIL loop is deterministic, and vcan0 and socketpair transports give identical traces. | — (test infrastructure) | `sil/test_transport.py` (vcan comparison runs only when vcan0 exists) |

| Scenario | Peak cell (°C) | Overshoot (°C) | Settling (s) | Charge 10→80 % (min) | Max state | Fault flags set |
|---|---|---|---|---|---|---|
| fast_charge_25C | 36.2 | 1.21 | 629 | 30.9 | NORMAL | 0 |
| fast_charge_35C | 36.4 | 1.38 | 0 | 30.9 | NORMAL | 0 |
| fast_charge_45C | 45.0 | 0.04 | 1205 | 32.8 | DERATE | 0 |

| Injected fault | Expected reaction | First reaction | Latency (ms) | FTTI (ms) | Final state |
|---|---|---|---|---|---|
| sensor_a_stuck | DERATE | DERATE | 130 | 200 | DERATE |
| sensor_a_open | DERATE | DERATE | 20 | 200 | DERATE |
| sensor_a_short | DERATE | DERATE | 20 | 200 | DERATE |
| sensor_a_spike | DERATE | DERATE | 10 | 200 | DERATE |
| sensor_ab_implausible | LIMP | LIMP | 40 | 200 | LIMP |
| sensor_both_open | SAFE_STATE | SAFE_STATE | 20 | 200 | SAFE_STATE |
| temps_frame_drop | SAFE_STATE | SAFE_STATE | 100 | 200 | SAFE_STATE |
| pack_frame_drop | LIMP | LIMP | 100 | 200 | LIMP |
| crc_corrupt | LIMP | LIMP | 80 | 200 | LIMP |
| alive_frozen | LIMP | LIMP | 20 | 200 | SAFE_STATE |

# Testing

## Suites

```bash
make -C firmware test        # 47 Unity tests, gcov branch report, fails below 80 % per module
python3 -m pytest sil -q     # builds bms_app, runs 3 fast charges + 10 fault scenarios (~1 min)
```

- `firmware/tests/`: Unity unit tests for the controller library.
- `sil/test_closed_loop.py`: fast charges at 25, 35 and 45 degC ambient (peak temperature, overshoot, settling, charge time, no false trips).
- `sil/test_faults.py`: the 10 injected faults, each checked for the expected reaction within the 200 ms FTTI.
- `sil/test_dbc_codec.py`: the C codec against cantools on all 31 DBC signals (random, edge and physical-value round trips).
- `sil/test_transport.py`: transport determinism.

The pytest run also writes `sil/out/results.md` and `sil/out/results.json`. `sil/conftest.py` falls back to the packages in `.venv/` when the interpreter running pytest lacks cantools. The GitHub Actions workflow (`.github/workflows/ci.yml`) runs both suites and tries to bring up `vcan0` first.

## Reading the results table

Overshoot is measured above max(setpoint 35 degC, starting temperature). Settling is the time until the cell temperature enters 35 +/- 1.5 degC and stays there while charging. The 45 degC pack starts soaked at ambient, so its settling value is the pull-down time.

`alive_frozen` first reaches LIMP (alive-counter error) and then SAFE_STATE, because frames with a frozen counter are rejected and the cell-temperature timeout follows.

## Coverage (from `make -C firmware test`)

| Module | Lines % | Branches % |
|---|---|---|
| bms_core.c | 99.3 | 82.1 |
| can_codec.c | 100.0 | 95.8 |
| derate.c | 100.0 | 100.0 |
| e2e.c | 100.0 | 100.0 |
| fault_detect.c | 100.0 | 95.5 |
| filter.c | 100.0 | 100.0 |
| pi_ctrl.c | 100.0 | 83.3 |
| state_machine.c | 100.0 | 100.0 |
| Total | 99.8 | 91.5 |

## Requirement traceability

The full matrix and fault-reaction table are in [safety_requirements.md](safety_requirements.md).

| Req ID | What | Tests |
|---|---|---|
| REQ-THM-01..04 | Peak temperature, overshoot, settling, bounded/slew-limited PI | `sil/test_closed_loop.py`, `firmware/tests/test_filter_pi.c` |
| REQ-CHG-01..02 | Derate map with hysteresis, charge time | `firmware/tests/test_derate.c`, `sil/test_closed_loop.py` |
| REQ-FD-01..04 | Range, stuck-at, rate-of-change, plausibility | `firmware/tests/test_fault_detect.c`, `test_bms_core.c`, `sil/test_faults.py` |
| REQ-E2E-01..03 | CRC-8, alive counter, timeout | `firmware/tests/test_e2e.c`, `test_bms_core.c`, `sil/test_faults.py`, `sil/test_dbc_codec.py` |
| REQ-SM-01..02 | State machine, latching, safe-state outputs | `firmware/tests/test_state_machine.c`, `test_bms_core.c`, `sil/test_faults.py` |
| REQ-SAF-01..03 | Reaction within FTTI, no false trips, watchdog reaction | `sil/test_faults.py`, `sil/test_closed_loop.py`, `test_bms_core.c` |
| REQ-CAN-01..02 | DBC codec vs cantools, transport determinism | `sil/test_dbc_codec.py`, `firmware/tests/test_can_codec.c`, `sil/test_transport.py` |

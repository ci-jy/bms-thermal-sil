# SIL harness reference

## bms_app

```
bms_app (--can IFACE | --fd N) [--realtime]
```

- `--can IFACE`: SocketCAN raw socket on the given interface, e.g. `vcan0`.
- `--fd N`: use an already-open `AF_UNIX` `SOCK_SEQPACKET` socket (one end of a socketpair) carrying the same 16-byte `struct can_frame`.
- `--realtime`: release the control task from the FreeRTOS tick with `vTaskDelayUntil` instead of once per `SIL_Sync` frame. Not covered by any test.

## Transport selection

The Python side (`sil/bmssil/transport.py`) uses `vcan0` whenever the interface exists and falls back to the socketpair otherwise. Over vcan, scenarios run one after another on the shared bus. Set `BMS_TRANSPORT=vcan` or `BMS_TRANSPORT=socketpair` to force one. pytest prints the chosen transport in its header.

Setting up vcan:

```bash
sudo modprobe vcan && sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0
```

## Lockstep messages

Each 10 ms step the plant sends `PLANT_CellTemps`, `PLANT_PackStatus` and `SIL_Sync`. The controller runs one control cycle per `SIL_Sync` and replies with `BMS_ChargeRequest` and `BMS_ThermalCmd`. The 100 ms diagnostics task sends `BMS_FaultStatus`.

## FreeRTOS tasks

- CAN task: moves frames between the transport and the rx/tx queues.
- Control task, 10 ms: fault detection, state machine, PI cooling, derate map.
- Diagnostics task, 100 ms: checks that the control and CAN tasks made progress (watchdog) and sends `BMS_FaultStatus`.

All tasks and queues are statically allocated.

## Plant (`sil/bmssil/plant.py`)

- Two thermal masses: 350 kJ/K cells, 18 kJ/K coolant.
- Cell-to-coolant conductance depends on pump speed, 150 to 600 W/K.
- 60 mOhm pack resistance (I^2 R heating).
- 7 kW chiller with low-coolant cut-off.
- Ambient losses.
- Seeded sensor noise (0.2 degC on the cell sensors; the coolant channel is noise-free).
- CC-taper fast charger: 300 A to 50 % SOC, tapering to 120 A at 80 %.

## Controller design notes

- Fused temperature is the max of the two healthy sensors, so the controller errs hot. A sample that fails the range or rate check is excluded immediately, before its debounce qualifies.
- Fault reactions latch for the drive cycle. Thermal derate does not: it recovers with 45/43 degC hysteresis.
- E2E is CRC-8 SAE J1850 with a data ID, a 4-bit alive counter and a timeout. Errors are debounced per message: +2 per bad frame, -1 per good frame, limit 6.
- Stuck-at detection fires after 15 identical 12-bit raw samples in a row. It relies on sensor noise, so the noise-free coolant channel is excluded from the stuck check.
- The PI controller uses clamping plus conditional-integration anti-windup, followed by a slew limiter.
- The derate map uses a 2 degC play-operator hysteresis.

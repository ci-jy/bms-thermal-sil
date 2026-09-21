"""Lockstep SIL runner: plant + fault injector <-> FreeRTOS controller over CAN.

Per 10 ms step k the plant sends PLANT_CellTemps, PLANT_PackStatus and
SIL_Sync(k); the controller runs one control cycle and answers with
BMS_ChargeRequest and BMS_ThermalCmd (plus BMS_FaultStatus every 100 ms).
The plant then integrates 10 ms with those commands.
"""
from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from .dbc import STATE_IDS, STATE_NAMES, Codecs, e2e_crc, e2e_protect
from .faults import FAULTS, FaultInjector
from .plant import ChargerProfile, PackPlant, PlantParams
from .transport import make_transport, spawn_firmware

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = Path(os.environ.get("BMS_APP", ROOT / "firmware" / "build" / "bms_app"))
DT = 0.01
SETPOINT_C = 35.0
T_LIMIT_C = 50.0          # peak cell temperature limit during fast charge (REQ-THM-01)
FTTI_MS = 200.0           # fault-tolerant time interval (SG-2, SG-3)
REPLY_TIMEOUT_S = 5.0

ID_SYNC, ID_TEMPS, ID_PACK = 0x050, 0x100, 0x110
ID_CMD, ID_REQ, ID_DIAG = 0x200, 0x210, 0x300


@dataclass
class Scenario:
    name: str
    ambient_c: float
    t_pack0_c: float | None = None
    soc0: float = 0.10
    duration_s: float | None = None     # None: until the charger stops (SOC end) + 10 s
    max_duration_s: float = 3600.0
    fault: str | None = None
    inject_at_s: float = 3.0
    seed: int = 1
    charger: ChargerProfile = field(default_factory=ChargerProfile)


@dataclass
class Result:
    scenario: Scenario
    trace: dict
    metrics: dict
    diag_frames: list


class FirmwareError(RuntimeError):
    pass


def run_scenario(sc: Scenario, transport: str = "socketpair", firmware: Path = FIRMWARE) -> Result:
    codecs = Codecs()
    c_temps, c_pack, c_sync = codecs["PLANT_CellTemps"], codecs["PLANT_PackStatus"], codecs["SIL_Sync"]
    c_cmd, c_req, c_diag = codecs["BMS_ThermalCmd"], codecs["BMS_ChargeRequest"], codecs["BMS_FaultStatus"]

    t0 = sc.ambient_c if sc.t_pack0_c is None else sc.t_pack0_c
    plant = PackPlant(PlantParams(), sc.ambient_c, t0, sc.soc0, sc.charger, seed=sc.seed)
    inj = FaultInjector(sc.fault, int(round(sc.inject_at_s / DT)) if sc.fault else 0)

    tr = transport if not isinstance(transport, str) else make_transport(transport)
    proc = spawn_firmware(str(firmware), tr)
    rows, diag = [], []
    pump = chiller = i_req = 0.0
    enable = False
    end_step = int(round(sc.duration_s / DT)) if sc.duration_s else int(sc.max_duration_s / DT)
    charge_done_step = None
    try:
        for k in range(end_step):
            # ---- plant -> BMS
            temps = inj.cell_temps(k, list(plant.sensors()))
            data = c_temps.encode({"T_CellA": temps[0], "T_CellB": temps[1],
                                   "T_CoolantIn": temps[2], "T_CoolantOut": temps[3]})
            raw_a = inj.stuck_raw(k, c_temps.raw(data, "T_CellA"))
            data[0] = raw_a & 0xFF
            data[1] = (data[1] & 0xF0) | (raw_a >> 8)
            e2e_protect(ID_TEMPS, data, inj.alive_counter(k, k & 0x0F))
            if inj.corrupt_crc(k, "PLANT_CellTemps"):
                data[7] ^= 0x5A
            if not inj.drop(k, "PLANT_CellTemps"):
                tr.send(ID_TEMPS, data)

            data = c_pack.encode({"PackCurrent": plant.current, "PackVoltage": plant.voltage(),
                                  "PackSOC": plant.soc * 100.0})
            e2e_protect(ID_PACK, data, k & 0x0F)
            if not inj.drop(k, "PLANT_PackStatus"):
                tr.send(ID_PACK, data)
            tr.send(ID_SYNC, c_sync.encode({"SIL_StepCounter": k}))

            # ---- BMS -> plant (wait for the end-of-step ThermalCmd)
            cmd = None
            while cmd is None:
                frame = tr.recv(REPLY_TIMEOUT_S)
                if frame is None:
                    raise FirmwareError(f"no reply at step {k}; firmware rc={proc.poll()}")
                fid, payload = frame
                if e2e_crc(fid, payload) != payload[7]:
                    raise FirmwareError(f"bad CRC from controller on 0x{fid:x}")
                if fid == ID_REQ:
                    req = c_req.decode(payload)
                    i_req, enable = req["ChargeCurrentReq"], bool(req["ChargeEnable"])
                elif fid == ID_DIAG:
                    d = c_diag.decode(payload)
                    d["step"] = k
                    diag.append(d)
                elif fid == ID_CMD:
                    cmd = c_cmd.decode(payload)
            pump, chiller = cmd["PumpDutyCmd"] / 100.0, cmd["ChillerDutyCmd"] / 100.0
            rows.append((k * DT, plant.t_pack, plant.t_cool, plant.current, plant.soc, pump, chiller,
                         i_req, int(cmd["BmsState"]), int(cmd["FaultFlags"]), int(cmd["FaultLatched"]),
                         temps[0], temps[1]))

            plant.step(DT, i_req, enable, pump, chiller)
            if sc.duration_s is None:
                if charge_done_step is None and plant.soc >= sc.charger.soc_end:
                    charge_done_step = k
                if charge_done_step is not None and k - charge_done_step >= int(10.0 / DT):
                    break
    finally:
        tr.close()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
            proc.wait()

    cols = ["t", "t_pack", "t_cool", "current", "soc", "pump", "chiller", "i_req",
            "state", "flags", "latched", "sens_a", "sens_b"]
    arr = np.array(rows, dtype=float)
    trace = {c: arr[:, i] for i, c in enumerate(cols)}
    return Result(sc, trace, compute_metrics(sc, trace, inj), diag)


def compute_metrics(sc: Scenario, tr: dict, inj: FaultInjector) -> dict:
    t, tp, state = tr["t"], tr["t_pack"], tr["state"].astype(int)
    m = {
        "peak_cell_c": float(tp.max()),
        "max_state": STATE_NAMES[int(state.max())],
        "fault_steps": int(np.count_nonzero(tr["flags"])),
        "latched": bool(tr["latched"].any()),
        "final_soc": float(tr["soc"][-1]),
    }
    start = float(tp[0])
    m["overshoot_c"] = max(0.0, m["peak_cell_c"] - max(SETPOINT_C, start))
    reached = np.nonzero(tr["soc"] >= sc.charger.soc_end)[0]
    m["charge_time_s"] = float(t[reached[0]]) if reached.size else None
    # Settling: last time the cell temperature was outside setpoint +/- 2 degC
    # while charging, i.e. from then on it stays in band or below it.
    charging = tr["current"] > 1.0
    outside = (tp > SETPOINT_C + 2.0) & charging
    if not outside.any():
        m["settling_time_s"] = 0.0
    else:
        last = np.nonzero(outside)[0][-1]
        m["settling_time_s"] = float(t[last]) if last < len(t) - 1 else None
    if sc.fault:
        k0 = inj.inject_step
        expected = FAULTS[sc.fault][0]
        pre = state[:k0]
        m["pre_fault_state"] = STATE_NAMES[int(pre[-1])]
        m["pre_fault_flags"] = int(np.count_nonzero(tr["flags"][:k0]))
        post = np.nonzero(state[k0:] != state[k0 - 1])[0]
        if post.size:
            k1 = k0 + int(post[0])
            m["first_reaction"] = STATE_NAMES[int(state[k1])]
            m["detection_latency_ms"] = (k1 - k0) * DT * 1000.0
        else:
            m["first_reaction"] = None
            m["detection_latency_ms"] = None
        m["expected_reaction"] = expected
        hit = np.nonzero(state[k0:] >= STATE_IDS[expected])[0]
        m["reaction_latency_ms"] = float(hit[0]) * DT * 1000.0 if hit.size else None
        m["final_state"] = STATE_NAMES[int(state[-1])]
    return m

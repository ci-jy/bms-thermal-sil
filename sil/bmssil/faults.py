"""Fault injector: manipulates plant->BMS frames after a given step."""
from __future__ import annotations

from dataclasses import dataclass

# kind -> (expected first reaction state, description)
FAULTS = {
    "sensor_a_stuck": ("DERATE", "cell sensor A frozen at its last value"),
    "sensor_a_open": ("DERATE", "cell sensor A open circuit (reads full scale)"),
    "sensor_a_short": ("DERATE", "cell sensor A shorted (reads bottom of scale)"),
    "sensor_a_spike": ("DERATE", "cell sensor A jumps +15 degC on alternate samples (intermittent connector)"),
    "sensor_ab_implausible": ("LIMP", "sensor A reads 6 degC high; both in range"),
    "sensor_both_open": ("SAFE_STATE", "both cell sensors open"),
    "temps_frame_drop": ("SAFE_STATE", "PLANT_CellTemps frames no longer received"),
    "pack_frame_drop": ("LIMP", "PLANT_PackStatus frames no longer received"),
    "crc_corrupt": ("LIMP", "every 2nd PLANT_CellTemps frame has a corrupted CRC"),
    "alive_frozen": ("LIMP", "PLANT_CellTemps alive counter stops incrementing"),
}


@dataclass
class FaultInjector:
    kind: str | None = None
    inject_step: int = 0

    def __post_init__(self):
        self._stuck_raw = None
        self._frozen_counter = None

    def active(self, step: int) -> bool:
        return self.kind is not None and step >= self.inject_step

    def cell_temps(self, step: int, temps: list[float]) -> list[float]:
        """Physical-value manipulation before encoding; returns temps A, B, in, out."""
        if not self.active(step):
            return temps
        a, b, cin, cout = temps
        if self.kind == "sensor_a_open":
            a = 369.5
        elif self.kind == "sensor_a_short":
            a = -40.0
        elif self.kind == "sensor_a_spike":
            a = a + 15.0 if (step - self.inject_step) % 2 == 0 else a
        elif self.kind == "sensor_ab_implausible":
            a = a + 6.0
        elif self.kind == "sensor_both_open":
            a = b = 369.5
        return [a, b, cin, cout]

    def stuck_raw(self, step: int, raw_a: int) -> int:
        if self.kind != "sensor_a_stuck":
            return raw_a
        if not self.active(step):
            self._stuck_raw = raw_a
            return raw_a
        return self._stuck_raw if self._stuck_raw is not None else raw_a

    def alive_counter(self, step: int, counter: int) -> int:
        if self.kind == "alive_frozen" and self.active(step):
            if self._frozen_counter is None:
                self._frozen_counter = (counter - 1) & 0x0F
            return self._frozen_counter
        return counter

    def drop(self, step: int, message: str) -> bool:
        if not self.active(step):
            return False
        return (self.kind == "temps_frame_drop" and message == "PLANT_CellTemps") or \
               (self.kind == "pack_frame_drop" and message == "PLANT_PackStatus")

    def corrupt_crc(self, step: int, message: str) -> bool:
        return (self.kind == "crc_corrupt" and message == "PLANT_CellTemps"
                and self.active(step) and (step - self.inject_step) % 2 == 0)

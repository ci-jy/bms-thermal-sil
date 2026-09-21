"""Lumped-parameter pack/coolant thermal plant with a DC fast charger.

Two thermal masses (cells, coolant loop) coupled by a pump-dependent
conductance; I^2R heating in the cells; a chiller whose capacity falls off as
the coolant approaches its freeze-protection limit; losses to ambient.
Parameters are order-of-magnitude values for a ~65 kWh, 96s passenger-car
pack with a liquid cold plate. Explicit Euler at the 10 ms control step
(the fastest time constant, C_cool / UA_max, is ~30 s).
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np


@dataclass
class PlantParams:
    c_pack_j_per_k: float = 350e3      # 350 kg of cells x ~1 kJ/(kg K)
    c_cool_j_per_k: float = 18e3       # ~5 L glycol loop + cold plate
    r_int_ohm_25c: float = 0.060       # 96 cells x 0.625 mOhm
    r_int_temp_coeff: float = -0.012   # resistance falls ~1.2 %/K when warm
    ua_pack_cool_min: float = 150.0    # W/K at zero pump flow (cold-plate conduction)
    ua_pack_cool_max: float = 600.0    # W/K at full pump flow
    ua_pack_amb: float = 15.0          # W/K pack enclosure to ambient
    ua_cool_amb: float = 40.0          # W/K coolant lines to ambient
    q_chiller_max_w: float = 7000.0
    chiller_cutoff_c: float = 5.0      # chiller capacity -> 0 as coolant approaches this
    capacity_ah: float = 180.0
    n_series: int = 96
    sensor_noise_c: float = 0.2        # 1-sigma ADC/NTC noise per sample
    sensor_offset_b_c: float = -0.3    # static offset of the redundant sensor


@dataclass
class ChargerProfile:
    """DC fast-charge current the charger offers vs SOC (CC with taper)."""
    i_max_a: float = 300.0
    taper_start_soc: float = 0.50
    i_end_a: float = 120.0
    soc_end: float = 0.80
    slew_a_per_s: float = 200.0

    def offered(self, soc: float) -> float:
        if soc >= self.soc_end:
            return 0.0
        if soc <= self.taper_start_soc:
            return self.i_max_a
        w = (soc - self.taper_start_soc) / (self.soc_end - self.taper_start_soc)
        return self.i_max_a + w * (self.i_end_a - self.i_max_a)


class PackPlant:
    def __init__(self, params: PlantParams, ambient_c: float, t_pack0_c: float,
                 soc0: float, charger: ChargerProfile, seed: int = 0):
        self.p = params
        self.ambient = ambient_c
        self.t_pack = t_pack0_c
        self.t_cool = t_pack0_c
        self.soc = soc0
        self.current = 0.0
        self.charger = charger
        self.rng = np.random.default_rng(seed)
        self.q_chiller = 0.0

    def ocv(self) -> float:
        return self.p.n_series * (3.40 + 0.75 * self.soc)

    def r_int(self) -> float:
        return self.p.r_int_ohm_25c * max(0.5, 1.0 + self.p.r_int_temp_coeff * (self.t_pack - 25.0))

    def voltage(self) -> float:
        return self.ocv() + self.current * self.r_int()

    def sensors(self):
        """Cell sensor A, redundant sensor B, coolant in/out (degC)."""
        n = self.rng.normal(0.0, self.p.sensor_noise_c, size=4)
        return (self.t_pack + n[0],
                self.t_pack + self.p.sensor_offset_b_c + n[1],
                self.t_cool - self.q_chiller / 2000.0 + n[2],
                self.t_cool + n[3])

    def step(self, dt: float, i_request: float, enable: bool, pump: float, chiller: float):
        target = min(self.charger.offered(self.soc), i_request) if enable else 0.0
        max_step = self.charger.slew_a_per_s * dt
        self.current += float(np.clip(target - self.current, -max_step, max_step))
        if not enable:
            self.current = 0.0  # contactor/charger cuts current immediately

        p = self.p
        q_gen = self.current ** 2 * self.r_int()
        ua = p.ua_pack_cool_min + (p.ua_pack_cool_max - p.ua_pack_cool_min) * pump
        q_pc = ua * (self.t_pack - self.t_cool)
        capacity = np.clip((self.t_cool - p.chiller_cutoff_c) / 10.0, 0.0, 1.0)
        self.q_chiller = chiller * p.q_chiller_max_w * capacity if pump > 0.05 else 0.0
        d_pack = (q_gen - q_pc - p.ua_pack_amb * (self.t_pack - self.ambient)) / p.c_pack_j_per_k
        d_cool = (q_pc - self.q_chiller - p.ua_cool_amb * (self.t_cool - self.ambient)) / p.c_cool_j_per_k
        self.t_pack += d_pack * dt
        self.t_cool += d_cool * dt
        self.soc += self.current * dt / (p.capacity_ah * 3600.0)

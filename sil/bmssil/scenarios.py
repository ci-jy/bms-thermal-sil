"""Scenario catalogue: fast-charge profiles at 25/35/45 degC ambient and
seeded fault-injection runs."""
from __future__ import annotations

from .faults import FAULTS
from .runner import Scenario

AMBIENTS_C = (25.0, 35.0, 45.0)


def charge_scenarios():
    """DC fast charge 10 -> 80 % SOC from a pack soaked at ambient."""
    return [Scenario(f"fast_charge_{int(a)}C", ambient_c=a, soc0=0.10, seed=100 + i)
            for i, a in enumerate(AMBIENTS_C)]


def fault_scenarios():
    """Fault injected 3 s into a 30 % SOC charge at 25 degC; 2 s observed after."""
    return [Scenario(f"fault_{kind}", ambient_c=25.0, t_pack0_c=30.0, soc0=0.30,
                     duration_s=5.0, fault=kind, inject_at_s=3.0, seed=200 + i)
            for i, kind in enumerate(FAULTS)]


def format_table(results) -> str:
    lines = ["| Scenario | Peak cell (°C) | Overshoot (°C) | Settling (s) | Charge 10→80 % (min) | Max state | Fault flags set |",
             "|---|---|---|---|---|---|---|"]
    for r in results:
        m = r.metrics
        if r.scenario.fault:
            continue
        settle = "—" if m["settling_time_s"] is None else f"{m['settling_time_s']:.0f}"
        chg = "—" if m["charge_time_s"] is None else f"{m['charge_time_s'] / 60:.1f}"
        lines.append(f"| {r.scenario.name} | {m['peak_cell_c']:.1f} | {m['overshoot_c']:.2f} | {settle} | "
                     f"{chg} | {m['max_state']} | {m['fault_steps']} |")
    faults = [r for r in results if r.scenario.fault]
    if faults:
        lines += ["", "| Injected fault | Expected reaction | First reaction | Latency (ms) | FTTI (ms) | Final state |",
                  "|---|---|---|---|---|---|"]
        for r in faults:
            m = r.metrics
            lat = "—" if m["reaction_latency_ms"] is None else f"{m['reaction_latency_ms']:.0f}"
            lines.append(f"| {r.scenario.fault} | {m['expected_reaction']} | {m['first_reaction']} | {lat} | 200 | "
                         f"{m['final_state']} |")
    return "\n".join(lines) + "\n"

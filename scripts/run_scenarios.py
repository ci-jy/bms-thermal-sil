#!/usr/bin/env python3
"""Run every SIL scenario against the FreeRTOS controller and write the
results table (docs/results.md, docs/results.json) and the figure
(docs/sil_results.png)."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "sil"))

from bmssil.runner import FTTI_MS, SETPOINT_C, T_LIMIT_C, run_many  # noqa: E402
from bmssil.scenarios import charge_scenarios, fault_scenarios, format_table  # noqa: E402


def plot(charge, faults, path: Path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(1, 3, figsize=(15, 4.4), gridspec_kw={"width_ratios": [1.2, 1.2, 1]})
    ax_t, ax_i, ax_f = axes
    for r in charge:
        tr = r.trace
        label = f"{r.scenario.ambient_c:.0f} °C ambient"
        ax_t.plot(tr["t"] / 60, tr["t_pack"], label=label)
        ax_i.plot(tr["t"] / 60, tr["current"], label=label)
    ax_t.axhline(SETPOINT_C, color="grey", ls="--", lw=1, label="setpoint")
    ax_t.axhline(T_LIMIT_C, color="red", ls=":", lw=1, label="limit")
    ax_t.set(xlabel="time (min)", ylabel="cell temperature (°C)", title="Fast charge 10→80 % SOC: cell temperature")
    ax_i.set(xlabel="time (min)", ylabel="charge current (A)", title="Charge current (after BMS derating)")
    ax_t.legend(fontsize=8)
    ax_i.legend(fontsize=8)
    names = [r.scenario.fault for r in faults]
    lat = [r.metrics["reaction_latency_ms"] for r in faults]
    colors = {"DERATE": "tab:orange", "LIMP": "tab:purple", "SAFE_STATE": "tab:red"}
    ax_f.barh(names, lat, color=[colors[r.metrics["expected_reaction"]] for r in faults])
    ax_f.axvline(FTTI_MS, color="red", ls=":", label=f"FTTI {FTTI_MS:.0f} ms")
    ax_f.set(xlabel="fault → reaction latency (ms)", title="Injected faults (colour = reaction)")
    ax_f.invert_yaxis()
    ax_f.legend(fontsize=8)
    for ax in axes:
        ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(path, dpi=110)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=str(ROOT / "docs"))
    ap.add_argument("--no-plot", action="store_true")
    args = ap.parse_args()
    subprocess.run(["make", "-s", "-C", str(ROOT / "firmware"), "app"], check=True)
    charge = run_many(charge_scenarios())
    faults = run_many(fault_scenarios())
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    table = format_table(charge + faults)
    (out / "results.md").write_text(table)
    (out / "results.json").write_text(json.dumps(
        {r.scenario.name: r.metrics for r in charge + faults}, indent=2, default=str))
    if not args.no_plot:
        plot(charge, faults, out / "sil_results.png")
    print(table)


if __name__ == "__main__":
    main()

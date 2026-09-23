import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIL = Path(__file__).resolve().parent

# Use the project virtualenv's packages (cantools, python-can) when pytest is
# started from an interpreter that does not have them installed.
try:
    import cantools  # noqa: F401
except ImportError:
    for sp in sorted((ROOT / ".venv").glob("lib/python*/site-packages")):
        if sp.parent.name == f"python{sys.version_info.major}.{sys.version_info.minor}":
            sys.path.insert(0, str(sp))
sys.path.insert(0, str(SIL))

import pytest  # noqa: E402

from bmssil.scenarios import charge_scenarios, fault_scenarios, format_table  # noqa: E402
from bmssil.runner import run_many  # noqa: E402
from bmssil.transport import default_transport  # noqa: E402

OUT = SIL / "out"


def pytest_report_header(config):
    return f"SIL CAN transport: {default_transport()} (vcan0 when available, socketpair fallback)"


@pytest.fixture(scope="session")
def firmware_build():
    subprocess.run(["make", "-s", "-C", str(ROOT / "firmware"), "app", "tools"], check=True)
    return ROOT / "firmware" / "build"


@pytest.fixture(scope="session")
def charge_results(firmware_build):
    return run_many(charge_scenarios())


@pytest.fixture(scope="session")
def fault_results(firmware_build):
    return run_many(fault_scenarios())


@pytest.fixture(scope="session", autouse=True)
def results_table(request):
    collected = {}
    request.config._bms_results = collected
    yield collected
    if not collected:
        return
    OUT.mkdir(exist_ok=True)
    results = collected.get("charge", []) + collected.get("fault", [])
    (OUT / "results.md").write_text(format_table(results))
    (OUT / "results.json").write_text(json.dumps(
        {r.scenario.name: r.metrics for r in results}, indent=2, default=str))
    print(f"\nSIL results table written to {OUT / 'results.md'}")

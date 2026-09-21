"""Closed-loop fast-charge scenarios at 25/35/45 degC ambient (no faults)."""
import pytest

from bmssil.runner import SETPOINT_C, T_LIMIT_C

MAX_OVERSHOOT_C = 2.0
MAX_SETTLING_S = 1500.0
MAX_CHARGE_TIME_S = 40 * 60.0


@pytest.fixture(scope="module")
def results(charge_results, request):
    request.config._bms_results["charge"] = charge_results
    return {r.scenario.name: r for r in charge_results}


def test_all_ambients_covered(results):
    assert {r.scenario.ambient_c for r in results.values()} == {25.0, 35.0, 45.0}


def test_req_thm_01_peak_cell_temperature_below_limit(results):
    for name, r in results.items():
        assert r.metrics["peak_cell_c"] < T_LIMIT_C, name


def test_req_thm_02_overshoot_bounded(results):
    for name, r in results.items():
        assert r.metrics["overshoot_c"] <= MAX_OVERSHOOT_C, name


def test_req_thm_03_settles_into_band(results):
    for name, r in results.items():
        s = r.metrics["settling_time_s"]
        assert s is not None and s <= MAX_SETTLING_S, (name, s)


def test_req_thm_03_holds_setpoint_band_once_settled(results):
    for name, r in results.items():
        tr = r.trace
        after = (tr["t"] >= r.metrics["settling_time_s"]) & (tr["current"] > 1.0)
        assert abs(tr["t_pack"][after] - SETPOINT_C).max() <= 1.5 + 1e-9, name


def test_req_chg_02_charge_completes_in_time(results):
    for name, r in results.items():
        t = r.metrics["charge_time_s"]
        assert t is not None and t <= MAX_CHARGE_TIME_S, (name, t)


def test_req_saf_02_no_false_trips(results):
    for name, r in results.items():
        m = r.metrics
        assert m["fault_steps"] == 0, name
        assert not m["latched"], name
        assert m["max_state"] in ("NORMAL", "DERATE"), name


def test_req_chg_01_hot_pack_is_derated(results):
    hot = results["fast_charge_45C"]
    cool = results["fast_charge_25C"]
    assert hot.metrics["max_state"] == "DERATE"
    assert hot.trace["i_req"][100:6000].max() < cool.trace["i_req"][100:6000].max()


def test_req_sm_01_init_then_normal(results):
    r = results["fast_charge_25C"]
    states = r.trace["state"]
    assert states[0] == 0  # INIT on the first cycle
    assert (states[10:] >= 1).all()


def test_req_thm_04_actuators_bounded_and_rate_limited(results):
    for name, r in results.items():
        tr = r.trace
        for key, rate in (("pump", 0.5), ("chiller", 0.2)):
            assert tr[key].min() >= 0.0 and tr[key].max() <= 1.0, name
            # one count of the 0.5 % CAN resolution on top of the slew limit
            assert abs(tr[key][1:] - tr[key][:-1]).max() <= rate * 0.01 + 0.005 + 1e-9, (name, key)

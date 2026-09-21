"""Fault injection: every fault is detected and reaches its reaction within the FTTI."""
import pytest

from bmssil.faults import FAULTS
from bmssil.runner import FTTI_MS


@pytest.fixture(scope="module")
def results(fault_results, request):
    request.config._bms_results["fault"] = fault_results
    return {r.scenario.fault: r for r in fault_results}


def test_every_fault_kind_injected(results):
    assert set(results) == set(FAULTS)


@pytest.mark.parametrize("kind", sorted(FAULTS))
def test_req_saf_01_fault_reaction_within_ftti(results, kind):
    m = results[kind].metrics
    assert m["pre_fault_state"] == "NORMAL" and m["pre_fault_flags"] == 0
    assert m["first_reaction"] == m["expected_reaction"], m
    assert m["reaction_latency_ms"] is not None and m["reaction_latency_ms"] <= FTTI_MS, m


@pytest.mark.parametrize("kind", sorted(FAULTS))
def test_req_sm_01_fault_reaction_latches(results, kind):
    r = results[kind]
    k0 = r.scenario.inject_at_s / 0.01
    states = r.trace["state"]
    post = states[int(k0) + int(r.metrics["reaction_latency_ms"] / 10):]
    # severity never decreases after the reaction
    assert (post[1:] >= post[:-1]).all()
    assert r.trace["latched"][-1] == 1


@pytest.mark.parametrize("kind", ["sensor_both_open", "temps_frame_drop"])
def test_req_sm_02_safe_state_stops_charging(results, kind):
    r = results[kind]
    tr = r.trace
    safe = tr["state"] == 4
    assert safe.any()
    first = int(safe.argmax())
    assert (tr["i_req"][first:] == 0).all()
    # charger current is cut on the step after the request goes to zero
    assert (tr["current"][first + 1:] == 0).all()
    # maximum cooling: pump at 100 %, chiller ramping up at its slew limit
    assert tr["pump"][-1] == 1.0
    chiller = tr["chiller"][first:]
    assert (chiller[1:] >= chiller[:-1]).all() and chiller[-1] > chiller[0]


def test_req_fd_04_limp_caps_current(results):
    r = results["sensor_ab_implausible"]
    tr = r.trace
    limp = tr["state"] == 3
    assert limp.any() and tr["i_req"][limp].max() <= 50.0 + 0.05


def test_req_fd_redundant_sensor_keeps_charging(results):
    """A single cell-sensor fault derates but keeps charging on the healthy channel."""
    for kind in ("sensor_a_stuck", "sensor_a_open", "sensor_a_short", "sensor_a_spike"):
        tr = results[kind].trace
        assert tr["state"][-1] == 2, kind
        assert tr["current"][-1] > 0.0, kind
        assert tr["i_req"][-1] <= 175.0 + 0.05, kind

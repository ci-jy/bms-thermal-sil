"""Transport equivalence: traces are bit-identical across runs and transports."""
import numpy as np
import pytest

from bmssil.runner import Scenario, run_scenario
from bmssil.transport import vcan_available


def short_scenario():
    return Scenario("transport_check", ambient_c=35.0, t_pack0_c=36.0, soc0=0.3,
                    duration_s=4.0, fault="crc_corrupt", inject_at_s=2.0, seed=42)


def assert_same(a, b):
    for key in a.trace:
        np.testing.assert_array_equal(a.trace[key], b.trace[key], err_msg=key)


def test_req_can_02_lockstep_trace_is_deterministic(firmware_build):
    assert_same(run_scenario(short_scenario(), "socketpair"), run_scenario(short_scenario(), "socketpair"))


@pytest.mark.skipif(not vcan_available(), reason="vcan0 not available (needs the vcan kernel module)")
def test_req_can_02_vcan_and_socketpair_traces_identical(firmware_build):
    assert_same(run_scenario(short_scenario(), "socketpair"), run_scenario(short_scenario(), "vcan"))

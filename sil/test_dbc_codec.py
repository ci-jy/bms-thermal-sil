"""The C pack/unpack code round-trips against cantools on every DBC signal."""
import random
import subprocess

import pytest

from bmssil.dbc import e2e_crc, load_db


@pytest.fixture(scope="module")
def codec(firmware_build):
    proc = subprocess.Popen([str(firmware_build / "dbc_codec_cli")], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, text=True)

    def call(line: str) -> str:
        proc.stdin.write(line + "\n")
        proc.stdin.flush()
        return proc.stdout.readline().strip()

    yield call
    proc.stdin.close()
    proc.wait()


def random_raw(sig, rng):
    if sig.is_signed:
        return rng.randint(-(1 << (sig.length - 1)), (1 << (sig.length - 1)) - 1)
    return rng.randint(0, (1 << sig.length) - 1)


def edge_raws(sig):
    if sig.is_signed:
        return [-(1 << (sig.length - 1)), -1, 0, 1, (1 << (sig.length - 1)) - 1]
    return [0, 1, (1 << sig.length) - 1]


@pytest.fixture(scope="module")
def db():
    return load_db()


def test_req_can_01_every_message_known_to_c_codec(db, codec):
    for msg in db.messages:
        out = codec(f"decode {msg.frame_id:x} 0000000000000000")
        names = [kv.split("=")[0] for kv in out.split()]
        assert names == [s.name for s in msg.signals], msg.name


def test_req_can_01_c_decode_matches_cantools(db, codec):
    rng = random.Random(7)
    for msg in db.messages:
        for _ in range(200):
            payload = bytes(rng.getrandbits(8) for _ in range(8))
            expected = msg.decode(payload, scaling=False, decode_choices=False)
            got = dict(kv.split("=") for kv in codec(f"decode {msg.frame_id:x} {payload.hex()}").split())
            for sig in msg.signals:
                assert int(got[sig.name]) == expected[sig.name], (msg.name, sig.name, payload.hex())


def test_req_can_01_c_encode_matches_cantools(db, codec):
    rng = random.Random(11)
    for msg in db.messages:
        cases = [{s.name: random_raw(s, rng) for s in msg.signals} for _ in range(200)]
        for sig in msg.signals:
            for v in edge_raws(sig):
                case = {s.name: 0 for s in msg.signals}
                case[sig.name] = v
                cases.append(case)
        for raw in cases:
            args = " ".join(f"{k}={v}" for k, v in raw.items())
            got = bytes.fromhex(codec(f"encode {msg.frame_id:x} {args}"))
            assert got == msg.encode(raw, scaling=False, strict=False).ljust(8, b"\0"), (msg.name, raw)
            assert msg.decode(got, scaling=False, decode_choices=False) == raw


def test_req_can_01_physical_values_round_trip(db, codec):
    rng = random.Random(3)
    for msg in db.messages:
        for _ in range(100):
            phys = {}
            for s in msg.signals:
                lo, hi = s.minimum, s.maximum
                phys[s.name] = round(rng.uniform(lo, hi) / s.scale) * s.scale
            args = " ".join(f"{k}={v!r}" for k, v in phys.items())
            got = bytes.fromhex(codec(f"phys {msg.frame_id:x} {args}"))
            dec = msg.decode(got, decode_choices=False)
            for s in msg.signals:
                assert dec[s.name] == pytest.approx(phys[s.name], abs=s.scale * 0.51), (msg.name, s.name)


def test_req_e2e_01_crc_matches_python_reference(db, codec):
    rng = random.Random(5)
    for msg in db.messages:
        if not any(s.name.endswith("_CRC") for s in msg.signals):
            continue
        for _ in range(100):
            payload = bytes(rng.getrandbits(8) for _ in range(8))
            got = bytes.fromhex(codec(f"e2e {msg.frame_id:x} {payload.hex()}"))
            assert got[:7] == payload[:7]
            assert got[7] == e2e_crc(msg.frame_id, got)

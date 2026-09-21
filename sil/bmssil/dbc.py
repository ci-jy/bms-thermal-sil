"""DBC access for the plant: cantools loads the database, and a small
precompiled packer built from the same signal layout handles the per-step
encode/decode (cantools is used as the reference in the codec tests)."""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import cantools

ROOT = Path(__file__).resolve().parents[2]
DBC_PATH = ROOT / "dbc" / "bms_thermal.dbc"

STATE_NAMES = {0: "INIT", 1: "NORMAL", 2: "DERATE", 3: "LIMP", 4: "SAFE_STATE"}
STATE_IDS = {v: k for k, v in STATE_NAMES.items()}


def load_db():
    return cantools.database.load_file(str(DBC_PATH))


def crc8_j1850(data: bytes, crc: int = 0xFF) -> int:
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1D) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def e2e_crc(can_id: int, data: bytes) -> int:
    return crc8_j1850(bytes([can_id & 0xFF]) + bytes(data[:7])) ^ 0xFF


def e2e_protect(can_id: int, data: bytearray, counter: int) -> bytearray:
    data[6] = (data[6] & 0xF0) | (counter & 0x0F)
    data[7] = e2e_crc(can_id, data)
    return data


@dataclass(frozen=True)
class _Sig:
    start: int
    length: int
    signed: bool
    scale: float
    offset: float


class FastCodec:
    """Little-endian bit packing for one message, layout taken from the DBC."""

    def __init__(self, message):
        self.frame_id = message.frame_id
        self.sigs = {
            s.name: _Sig(s.start, s.length, s.is_signed, float(s.scale), float(s.offset))
            for s in message.signals
        }

    def encode(self, values: dict) -> bytearray:
        word = 0
        for name, phys in values.items():
            s = self.sigs[name]
            raw = int(round((phys - s.offset) / s.scale))
            if s.signed:
                lo, hi = -(1 << (s.length - 1)), (1 << (s.length - 1)) - 1
            else:
                lo, hi = 0, (1 << s.length) - 1
            raw = min(max(raw, lo), hi) & ((1 << s.length) - 1)
            word |= raw << s.start
        return bytearray(word.to_bytes(8, "little"))

    def raw(self, data: bytes, name: str) -> int:
        s = self.sigs[name]
        v = (int.from_bytes(data, "little") >> s.start) & ((1 << s.length) - 1)
        if s.signed and v >> (s.length - 1):
            v -= 1 << s.length
        return v

    def decode(self, data: bytes) -> dict:
        word = int.from_bytes(data, "little")
        out = {}
        for name, s in self.sigs.items():
            v = (word >> s.start) & ((1 << s.length) - 1)
            if s.signed and v >> (s.length - 1):
                v -= 1 << s.length
            out[name] = v * s.scale + s.offset
        return out


class Codecs:
    def __init__(self):
        self.db = load_db()
        self.by_name = {m.name: FastCodec(m) for m in self.db.messages}

    def __getitem__(self, name: str) -> FastCodec:
        return self.by_name[name]

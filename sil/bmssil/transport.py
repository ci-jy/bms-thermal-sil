"""CAN transports for the plant side.

Both carry Linux `struct can_frame` (16 bytes: id, dlc, pad, 8 data bytes):
- SocketpairTransport: AF_UNIX SOCK_SEQPACKET pair; one end is inherited by
  the firmware process (`--fd N`). Works without the vcan kernel module.
- VcanTransport: SocketCAN raw socket via python-can on vcan0 (`--can vcan0`).
"""
from __future__ import annotations

import os
import select
import socket
import struct
import subprocess

CAN_FRAME_FMT = "=IB3x8s"
CAN_FRAME_SIZE = struct.calcsize(CAN_FRAME_FMT)


def vcan_available(iface: str = "vcan0") -> bool:
    if not hasattr(socket, "AF_CAN"):
        return False
    try:
        s = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
        s.bind((iface,))
        s.close()
        return True
    except OSError:
        return False


class SocketpairTransport:
    name = "socketpair"

    def __init__(self):
        self._ours, self._theirs = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)

    def firmware_args(self):
        return ["--fd", str(self._theirs.fileno())], (self._theirs.fileno(),)

    def after_spawn(self):
        self._theirs.close()

    def send(self, can_id: int, data: bytes):
        self._ours.sendall(struct.pack(CAN_FRAME_FMT, can_id, 8, bytes(data)))

    def recv(self, timeout: float):
        r, _, _ = select.select([self._ours], [], [], timeout)
        if not r:
            return None
        pkt = self._ours.recv(CAN_FRAME_SIZE)
        if len(pkt) != CAN_FRAME_SIZE:
            return None
        can_id, _dlc, data = struct.unpack(CAN_FRAME_FMT, pkt)
        return can_id & 0x1FFFFFFF, data

    def close(self):
        self._ours.close()


class VcanTransport:
    name = "vcan"

    def __init__(self, iface: str = "vcan0"):
        import can  # python-can

        self.iface = iface
        self.bus = can.Bus(channel=iface, interface="socketcan", receive_own_messages=False)
        self._can = can

    def firmware_args(self):
        return ["--can", self.iface], ()

    def after_spawn(self):
        pass

    def send(self, can_id: int, data: bytes):
        self.bus.send(self._can.Message(arbitration_id=can_id, data=bytes(data), is_extended_id=False))

    def recv(self, timeout: float):
        msg = self.bus.recv(timeout)
        if msg is None:
            return None
        return msg.arbitration_id, bytes(msg.data).ljust(8, b"\0")

    def close(self):
        self.bus.shutdown()


def spawn_firmware(binary: str, transport, extra_args=()):
    args, pass_fds = transport.firmware_args()
    proc = subprocess.Popen([binary, *args, *extra_args], pass_fds=pass_fds,
                            stdin=subprocess.DEVNULL, stderr=subprocess.PIPE)
    transport.after_spawn()
    return proc


def default_transport() -> str:
    """vcan0 when the interface exists, otherwise the socketpair fallback.
    BMS_TRANSPORT=vcan|socketpair overrides the choice."""
    forced = os.environ.get("BMS_TRANSPORT")
    if forced:
        return forced
    return "vcan" if vcan_available(os.environ.get("BMS_VCAN", "vcan0")) else "socketpair"


def make_transport(kind: str):
    if kind == "socketpair":
        return SocketpairTransport()
    if kind == "vcan":
        return VcanTransport(os.environ.get("BMS_VCAN", "vcan0"))
    raise ValueError(kind)

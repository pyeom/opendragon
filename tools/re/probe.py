"""Minimal protocol client (hidapi) for poking at a receiver from Python.

    pip install hidapi
    python probe.py list
    python probe.py 3554 03              # device online?
    python probe.py 25a7 08 0000 0a      # read 10 bytes of flash at 0x0000

Arguments after the VID: command (hex), optional address (hex) and either a length (1-2 hex digits)
or a payload (hex string). See docs/PROTOCOL.md for the commands.
"""
import sys
import time

import hid

VENDOR_PAGES = (0xFF01, 0xFF02)


def checksum(data):
    return (0x55 - sum(data)) & 0xFF


class Receiver:
    def __init__(self, vid):
        infos = [d for d in hid.enumerate(vid) if d["usage_page"] in VENDOR_PAGES]
        tx = next(d for d in infos if d["usage_page"] == 0xFF02)
        rx = next((d for d in infos if d["usage_page"] == 0xFF01 and d["interface_number"] == tx["interface_number"]), tx)
        # Compx receivers (separate 0xFF01 reply collection) take feature reports, CX ones output reports.
        self.feature = rx is not tx
        self.tx = hid.device()
        self.tx.open_path(tx["path"])
        self.rx = self.tx
        if self.feature:
            self.rx = hid.device()
            self.rx.open_path(rx["path"])
        self.rx.set_nonblocking(1)

    def request(self, cmd, addr=0, payload=b"", length=None, timeout=0.5):
        pkt = bytearray(17)
        pkt[0], pkt[1], pkt[3], pkt[4] = 0x08, cmd, addr >> 8, addr & 0xFF
        pkt[5] = len(payload) if length is None else length
        pkt[6 : 6 + len(payload)] = payload
        pkt[16] = checksum(pkt[:16])
        while self.rx.read(64):
            pass
        (self.tx.send_feature_report if self.feature else self.tx.write)(bytes(pkt))
        end = time.time() + timeout
        while time.time() < end:
            r = self.rx.read(64)
            if r and r[1] == cmd:
                return bytes(r)
            time.sleep(0.002)
        return None


def main():
    if len(sys.argv) < 2 or sys.argv[1] == "list":
        for d in hid.enumerate():
            if d["usage_page"] >= 0xFF00:
                print(f"{d['vendor_id']:04x}:{d['product_id']:04x} if={d['interface_number']} "
                      f"page={d['usage_page']:04x} {d['product_string']!r}")
        return
    vid = int(sys.argv[1], 16)
    cmd = int(sys.argv[2], 16)
    addr = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0
    payload, length = b"", None
    if len(sys.argv) > 4:
        arg = sys.argv[4]
        if len(arg) <= 2:
            length = int(arg, 16)
        else:
            payload = bytes.fromhex(arg)
    reply = Receiver(vid).request(cmd, addr, payload, length)
    print(reply.hex(" ") if reply else "no reply (mouse asleep?)")


if __name__ == "__main__":
    main()

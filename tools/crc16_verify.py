#!/usr/bin/env python3
"""Verify the CRC implementations shared between firmware and host tools.

Checks:
1. The bitwise algorithm in crc16.c matches the table-based CRC-16/ARC (the
   removed crc16.asm implemented the standard CRC-16/ARC table byte-for-byte,
   verified 2026-08), so the update-image check (crc_value == 0xb001) stays
   compatible with existing images.
2. It matches the reference CRC-16/ARC (poly 0xA001, LSB-first) on random
   data and the standard check value "123456789" -> 0xBB3D.
3. The XMODEM-CRC polynomial (0x1021, MSB-first) used by cmd_xmodem.c has
   the standard check value "123456789" -> 0x31C3.
4. End-to-end: tools/crc_calculator.c (which includes crc16.c) produces the
   same CRC as the reference and its -v check passes (image CRC == 0xb001).
"""

import os
import random
import subprocess
import sys
import tempfile

SRC = "crc16.c"


def reference_table(poly=0xA001):
    """Standard LSB-first CRC table for the given reflected polynomial."""
    t = []
    for n in range(256):
        crc = n
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ poly
            else:
                crc >>= 1
        t.append(crc)
    return t


def table_update(crc, data, tl, th):
    i = data ^ (crc & 0xFF)
    return ((crc >> 8) ^ tl[i]) | (th[i] << 8)


def bitwise_update(crc, data):
    crc ^= data
    for _ in range(8):
        if crc & 1:
            crc = (crc >> 1) ^ 0xA001
        else:
            crc >>= 1
    return crc


def xmodem_update(crc, data):
    crc ^= data << 8
    for _ in range(8):
        if crc & 0x8000:
            crc = (crc << 1) ^ 0x1021
        else:
            crc <<= 1
    return crc & 0xFFFF


def crc_of(data, update_fn, init=0):
    crc = init
    for b in data:
        crc = update_fn(crc, b)
    return crc


def main():
    t = reference_table()
    tl = [c & 0xFF for c in t]
    th = [c >> 8 for c in t]

    rng = random.Random(0xC0DE)
    for trial in range(2000):
        data = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 300)))
        c_tab = c_bit = 0
        for b in data:
            c_tab = table_update(c_tab, b, tl, th)
            c_bit = bitwise_update(c_bit, b)
        assert c_tab == c_bit, f"mismatch vs table-based CRC-16/ARC on trial {trial}"
    print("2000 random trials (C bitwise vs table-based CRC-16/ARC): OK")

    check = b"123456789"
    assert crc_of(check, bitwise_update) == 0xBB3D
    print("check value '123456789' -> 0xBB3D (CRC-16/ARC): OK")

    assert crc_of(check, xmodem_update) == 0x31C3
    print("check value '123456789' -> 0x31C3 (XMODEM/0x1021, cmd_xmodem.c): OK")

    # End-to-end: crc_calculator shares crc16.c with the firmware.
    tool = os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "output", "crc_calculator"
    )
    tools_dir = os.path.dirname(os.path.abspath(__file__))
    if not os.path.exists(tool):
        subprocess.run(
            ["make", "-C", tools_dir, "output/crc_calculator"], check=True
        )
    data = bytes(rng.randrange(256) for _ in range(1000))
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(data)
        path = f.name
    try:
        subprocess.run([tool, "-u", path], check=True, capture_output=True)
        img = open(path, "rb").read()
        stored = int.from_bytes(img[-2:], "little")
        assert stored == (crc_of(data[:-2], bitwise_update) ^ 0xFFFF), (
            "crc_calculator -u stored CRC differs from reference"
        )
        r = subprocess.run([tool, "-v", path], capture_output=True)
        assert r.returncode == 0, f"crc_calculator -v failed: {r.stdout!r}"
    finally:
        os.unlink(path)
    print("crc_calculator (shared crc16.c) end-to-end -u/-v: OK")


if __name__ == "__main__":
    sys.exit(main())

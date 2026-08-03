#!/usr/bin/env python3
"""Convert 8051 lcall/ljmp targets inside the assembler poly1305_mul
(BANK3) from "physical & 0xFFFF" to "bank offset" (target - 0x8000).

The real-device bank map is offset-based: code space X maps to physical
0x34000 + X for BANK3. sdld resolves the hand-written assembler's label
references as "physical & 0xFFFF"; on the device that would jump to
0x20000 + (phys & 0xFFFF) - 0x4000. This script rewrites those targets to the
bank offset. Instruction boundaries are tracked with the 8051 opcode
length table so that immediate bytes are never mistaken for lcall/ljmp.

Also rewrites calls from C (poly1305_blocks) into _poly1305_mul.
Usage: fix_bank_refs.py <firmware.img> [mul_size_hex]
"""
import sys

# 8051 opcode lengths
LEN = [
1,2,3,1,1,2,1,1, 1,1,1,1,1,1,1,1,
2,2,3,1,1,2,1,1, 1,1,1,1,1,1,1,1,
2,2,1,1,2,2,1,1, 1,1,1,1,1,1,1,1,
2,2,1,1,2,2,1,1, 1,1,1,1,1,1,1,1,
2,2,2,3,2,2,1,1, 1,1,1,1,1,1,1,1,
2,2,2,3,2,2,1,1, 1,1,1,1,1,1,1,1,
2,2,2,3,2,2,1,1, 1,1,1,1,1,1,1,1,
2,2,2,3,2,3,1,1, 2,2,2,2,2,2,2,2,
2,2,2,1,1,3,1,1, 2,2,2,2,2,2,2,2,
3,2,2,1,2,2,1,1, 2,2,2,2,2,2,2,2,
2,2,2,1,1,2,1,1, 2,2,2,2,2,2,2,2,
2,2,2,1,3,3,1,1, 2,2,2,2,2,2,2,2,
2,2,2,1,1,2,1,1, 2,2,2,2,2,2,2,2,
2,2,2,1,1,3,1,1, 2,2,2,2,2,2,2,2,
1,2,1,1,1,2,1,1, 1,1,1,1,1,1,1,1,
1,2,1,1,1,2,1,1, 1,1,1,1,1,1,1,1,
]

def main():
    path = sys.argv[1]
    mul_size = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x7AB
    img = bytearray(open(path, 'rb').read())

    pat = bytes([0xEC, 0x8E, 0xF0, 0xA4])
    start = None
    for i in range(0x34000, 0x34000 + 0x10000 - 4):
        if img[i:i+4] == pat:
            start = i
            break
    if start is None:
        print("fix_bank_refs: mul16 pattern not found, aborting", file=sys.stderr)
        sys.exit(1)

    end = start + mul_size
    print(f"fix_bank_refs: mul at 0x{start:05x}-0x{end:05x}")
    poly_mul_phys = start + 0x314
    poly_mul_off = poly_mul_phys - 0x34000

    # walk instruction boundaries; rewrite lcall/ljmp targets
    a = start
    n = 0
    while a < end - 2:
        op = img[a]
        ln = LEN[op]
        if op in (0x12, 0x02):
            t = (img[a+1] << 8) | img[a+2]
            if 0x4000 <= t <= 0xFFFF:
                newt = t - 0x8000
                img[a+1] = (newt >> 8) & 0xFF
                img[a+2] = newt & 0xFF
                n += 1
        a += ln
    print(f"fix_bank_refs: rewrote {n} refs inside mul")

    # rewrite calls from C into _poly1305_mul (target == phys & 0xFFFF)
    tgt = poly_mul_phys & 0xFFFF
    n_c = 0
    for a in range(len(img) - 2):
        if img[a] == 0x12:
            t = (img[a+1] << 8) | img[a+2]
            if t == tgt:
                img[a+1] = (poly_mul_off >> 8) & 0xFF
                img[a+2] = poly_mul_off & 0xFF
                n_c += 1
    print(f"fix_bank_refs: rewrote {n_c} calls into _poly1305_mul")

    open(path, 'wb').write(img)
    print("fix_bank_refs: done")

if __name__ == '__main__':
    main()

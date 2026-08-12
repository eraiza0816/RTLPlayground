#!/usr/bin/env python3
"""Validate the OEM upgrade image header (audit F9).

Mirrors installer/installer.c's verify_update_header() and
installer/updatebuilder.c's header/payload checksums:

  - magic 0x12345678 at image offset 0x00 (and the second header at
    offset 0x4012, which the installer reads from flash 0x1d000)
  - length field (+0x04) = image size - 0x14
  - header checksum (+0x08) = byte sum of the header with the +0x08
    field itself zero (updatebuilder computes it before writing it)
  - payload checksum (+0x0c) = byte sum of installer code
    [0x14, 0x4012) + 0xff*0x14 + payload [0x4026, image_size)

Runs as a test: python3 verify_image.py [image]
Exit 0 if the image is valid, 1 otherwise.
"""

import struct
import sys

HEADER_LENGTH = 0x14
HEADER_MAGIC = 0x12345678
SECOND_HEADER_OFFSET = 0x4012
PAYLOAD_OFFSET = 0x4026


def rd32(b, off):
    return struct.unpack(">I", b[off:off + 4])[0]


def check_image(img):
    errors = []

    if rd32(img, 0x00) != HEADER_MAGIC:
        errors.append("bad magic at 0x00")
    if rd32(img, SECOND_HEADER_OFFSET) != HEADER_MAGIC:
        errors.append("bad magic at second header 0x4012")

    image_size = len(img)
    length = rd32(img, 0x04)
    if length + HEADER_LENGTH != image_size:
        errors.append("length field %#x != image size %#x - 0x14"
                      % (length, image_size))

    # Header checksum: byte sum with the +0x08 field treated as zero.
    hsum = sum(img[:0x08]) + sum(img[0x0c:HEADER_LENGTH])
    if (hsum & 0xffffffff) != rd32(img, 0x08):
        errors.append("header checksum mismatch (calc %#x, field %#x)"
                      % (hsum & 0xffffffff, rd32(img, 0x08)))

    # Payload checksum: installer code + 0xff*0x14 + payload.
    psum = (sum(img[0x14:SECOND_HEADER_OFFSET])
            + 0xff * HEADER_LENGTH
            + sum(img[PAYLOAD_OFFSET:image_size])) & 0xffffffff
    if psum != rd32(img, 0x0c):
        errors.append("payload checksum mismatch (calc %#x, field %#x)"
                      % (psum, rd32(img, 0x0c)))

    # The two headers must be identical (the installer reads the second
    # one from flash 0x1d000).
    if img[:HEADER_LENGTH] != img[SECOND_HEADER_OFFSET:SECOND_HEADER_OFFSET + HEADER_LENGTH]:
        errors.append("second header does not match the first")

    return errors


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "output/rtlplayground_oem_upgrade.bin"
    img = open(path, "rb").read()
    errors = check_image(img)
    if errors:
        for e in errors:
            print("FAIL: %s" % e)
        return 1
    print("OK: %s (%#x bytes, header valid)" % (path, len(img)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

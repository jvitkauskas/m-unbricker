#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Make a RAM app (m-bootloader docs/SPEC.md 11.8) from a flat binary linked
for 0x01C30000: append the 8-byte trailer `APP1`, CRC16 (XMODEM, of what
comes before), board (1 FM-1, 2 SMK-37), format 1.

  app_trailer.py IN.bin OUT.bin fm1|smk37
"""
import struct, sys

APP_MAX = 0x4FD80


def crc16(data, state=0):
    for byte in data:
        state ^= byte << 8
        for _ in range(8):
            state = ((state << 1) ^ (0x1021 if state & 0x8000 else 0)) & 0xFFFF
    return state


code = open(sys.argv[1], "rb").read()
board = {"fm1": 1, "smk37": 2}[sys.argv[3]]
if len(code) % 2:
    code += b"\0"
out = code + b"APP1" + struct.pack("<HBB", crc16(code), board, 1)
if not 16 <= len(out) <= APP_MAX:
    sys.exit(f"{sys.argv[1]}: {len(out)} bytes with the trailer, more than {APP_MAX}")
open(sys.argv[2], "wb").write(out)
print(f"{sys.argv[2]}: {len(out)} bytes ({APP_MAX - len(out)} to spare)")

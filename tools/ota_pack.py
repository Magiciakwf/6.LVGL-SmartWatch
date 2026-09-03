#!/usr/bin/env python3
"""Build an AES-128-CTR encrypted OV-Watch image for OneNET OTA."""

from __future__ import annotations

import argparse
import os
import struct
import zlib
from pathlib import Path

MAGIC = 0x4F544131
FORMAT_VERSION = 2
HEADER_SIZE = 256
FLAG_AES128_CTR = 1
APP_ADDRESS = 0x08010000
APP_MAX_SIZE = 0x70000
DEFAULT_KEY = "2b7e151628aed2a6abf7158809cf4f3c"  # development only

SBOX = (
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
)
RCON = (0, 1, 2, 4, 8, 16, 32, 64, 128, 0x1B, 0x36)


def expand_key(key: bytes) -> bytes:
    expanded = bytearray(key)
    rcon_index = 1
    while len(expanded) < 176:
        temp = list(expanded[-4:])
        if len(expanded) % 16 == 0:
            temp = [SBOX[temp[1]] ^ RCON[rcon_index], SBOX[temp[2]],
                    SBOX[temp[3]], SBOX[temp[0]]]
            rcon_index += 1
        for value in temp:
            expanded.append(expanded[-16] ^ value)
    return bytes(expanded)


def xtime(value: int) -> int:
    return ((value << 1) ^ (0x1B if value & 0x80 else 0)) & 0xFF


def encrypt_block(expanded: bytes, block: bytes) -> bytes:
    state = bytearray(a ^ b for a, b in zip(block, expanded[:16]))
    for round_number in range(1, 11):
        state[:] = (SBOX[x] for x in state)
        state[:] = bytes((state[0],state[5],state[10],state[15],
                          state[4],state[9],state[14],state[3],
                          state[8],state[13],state[2],state[7],
                          state[12],state[1],state[6],state[11]))
        if round_number != 10:
            for column in range(4):
                i = column * 4
                a, b, c, d = state[i:i+4]
                total = a ^ b ^ c ^ d
                state[i] = a ^ total ^ xtime(a ^ b)
                state[i+1] = b ^ total ^ xtime(b ^ c)
                state[i+2] = c ^ total ^ xtime(c ^ d)
                state[i+3] = d ^ total ^ xtime(d ^ a)
        key_offset = round_number * 16
        state[:] = (x ^ y for x, y in zip(state, expanded[key_offset:key_offset+16]))
    return bytes(state)


def aes_ctr(data: bytes, key: bytes, iv: bytes) -> bytes:
    expanded = expand_key(key)
    counter = int.from_bytes(iv, "big")
    output = bytearray()
    for offset in range(0, len(data), 16):
        stream = encrypt_block(expanded, counter.to_bytes(16, "big"))
        chunk = data[offset:offset+16]
        output.extend(a ^ b for a, b in zip(chunk, stream))
        counter = (counter + 1) & ((1 << 128) - 1)
    return bytes(output)


def parse_version(text: str) -> int:
    if "." not in text:
        value = int(text, 0)
        if not 0 <= value <= 0xFFFFFFFF:
            raise argparse.ArgumentTypeError("version must fit uint32")
        return value
    parts = text.split(".")
    if len(parts) != 3 or any(not p.isdigit() for p in parts):
        raise argparse.ArgumentTypeError("semantic version must be MAJOR.MINOR.PATCH")
    major, minor, patch = map(int, parts)
    if any(not 0 <= p <= 255 for p in (major, minor, patch)):
        raise argparse.ArgumentTypeError("semantic version components must be 0..255")
    return (major << 24) | (minor << 16) | patch


def build_header(image: bytes, payload: bytes, version: int, iv: bytes,
                 key_id: int) -> bytes:
    fixed = struct.pack("<IHHIIIIIII", MAGIC, FORMAT_VERSION, HEADER_SIZE,
                        FLAG_AES128_CTR, version, APP_ADDRESS, len(image),
                        len(payload), zlib.crc32(image), zlib.crc32(payload))
    header = bytearray(fixed + iv + struct.pack("<II", key_id, 0) + bytes(196))
    assert len(header) == HEADER_SIZE
    header_crc = zlib.crc32(header)
    struct.pack_into("<I", header, 56, header_crc)
    return bytes(header)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="Keil application .bin")
    parser.add_argument("output", type=Path, help="encrypted .ota output")
    parser.add_argument("--version", required=True, type=parse_version,
                        help="uint32 or MAJOR.MINOR.PATCH")
    parser.add_argument("--key-hex", default=DEFAULT_KEY,
                        help="16-byte AES key; must match ota_config.h")
    parser.add_argument("--key-id", type=int, default=1)
    parser.add_argument("--iv-hex", help="16-byte IV; random when omitted")
    args = parser.parse_args()

    key = bytes.fromhex(args.key_hex)
    iv = bytes.fromhex(args.iv_hex) if args.iv_hex else os.urandom(16)
    if len(key) != 16 or len(iv) != 16:
        parser.error("AES key and IV must each be exactly 16 bytes")
    image = args.input.read_bytes()
    if not image or len(image) > APP_MAX_SIZE:
        parser.error(f"APP binary must be 1..{APP_MAX_SIZE} bytes")
    payload = aes_ctr(image, key, iv)
    package = build_header(image, payload, args.version, iv, args.key_id) + payload
    args.output.write_bytes(package)

    print(f"output={args.output}")
    print(f"version_u32=0x{args.version:08X}")
    print(f"image_size={len(image)}")
    print(f"package_size={len(package)}")
    print(f"iv={iv.hex()}")
    if args.key_hex.lower() == DEFAULT_KEY:
        print("WARNING: using the development AES key; replace it for production")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

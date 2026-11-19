#!/usr/bin/env python3
"""Writes the project's own tiny Xbox 360 test program as an unencrypted, uncompressed XEX, plus
the ReXGlue manifest to recompile it.

The program is 14 hand-assembled PowerPC instructions written for this project (no game code):

    li    r3, 0              ; sum = 0
    li    r4, 1              ; i = 1
    lis   r5, 0x02FA
    ori   r5, r5, 0xF080     ; n = 50,000,000
  loop:
    add   r3, r3, r4         ; sum += i
    addi  r4, r4, 1          ; i++
    cmpw  r4, r5
    ble   loop               ; while i <= n
    lis   r6, 0x8200
    stw   r3, 0x4000(r6)     ; result at 0x82004000 (sum mod 2^32)
    lis   r7, 0x600D
    ori   r7, r7, 0xF00D
    stw   r7, 0x4004(r6)     ; marker 0x600DF00D at 0x82004004
    blr                      ; return: the title's main thread ends

It is packed like ReXGlue's own unit-test images (tests/code_image.cpp and
tests/unit/base/xex_builder.h): a 64 KiB PE image (.text, .pdata, .data) at 0x82000000 in a XEX2
container with no encryption and no compression. Nothing is encrypted, so nothing is decrypted.

usage: make_test_xex.py <output folder>
writes <output folder>/default.xex and <output folder>/testgame_manifest.toml
"""
import os
import struct
import sys

LOAD_ADDRESS = 0x82000000
IMAGE_SIZE = 0x10000
TEXT_OFFSET, PDATA_OFFSET, DATA_OFFSET = 0x1000, 0x2000, 0x4000
TITLE_ID = 0x7E570001  # a made-up test title ID, not any real game's

CODE = [
    0x38600000,  # li    r3, 0
    0x38800001,  # li    r4, 1
    0x3CA002FA,  # lis   r5, 0x02FA
    0x60A5F080,  # ori   r5, r5, 0xF080
    0x7C632214,  # add   r3, r3, r4
    0x38840001,  # addi  r4, r4, 1
    0x7C042800,  # cmpw  cr0, r4, r5
    0x4081FFF4,  # ble   cr0, -12 (back to add)
    0x3CC08200,  # lis   r6, 0x8200
    0x90664000,  # stw   r3, 0x4000(r6)
    0x3CE0600D,  # lis   r7, 0x600D
    0x60E7F00D,  # ori   r7, r7, 0xF00D
    0x90E64004,  # stw   r7, 0x4004(r6)
    0x4E800020,  # blr
]


def pe_image() -> bytes:
    image = bytearray(IMAGE_SIZE)
    # DOS header (little-endian, as in every PE file).
    struct.pack_into("<HH", image, 0, 0x5A4D, 0x90)  # 'MZ', e_cblp
    struct.pack_into("<I", image, 0x3C, 0x80)         # e_lfanew
    # NT headers at 0x80.
    image[0x80:0x84] = b"PE\0\0"
    # FileHeader: Machine POWERPCBE, 3 sections, optional header size 0xE0, 32-bit machine.
    struct.pack_into("<HHIIIHH", image, 0x84, 0x01F2, 3, 0, 0, 0, 0xE0, 0x0100)
    opt = 0x98
    struct.pack_into("<H", image, opt + 0, 0x10B)        # PE32 magic
    struct.pack_into("<I", image, opt + 60, TEXT_OFFSET)  # SizeOfHeaders
    struct.pack_into("<H", image, opt + 68, 14)           # Subsystem: Xbox
    struct.pack_into("<II", image, opt + 96 + 3 * 8, PDATA_OFFSET, 8)  # exception directory
    sections = [
        (b".text", TEXT_OFFSET, max(0x200, 4 * len(CODE)), 0x20000000),   # execute
        (b".pdata", PDATA_OFFSET, 0x100, 0x40000040),                     # read, initialized
        (b".data", DATA_OFFSET, 0x100, 0xC0000040),                       # read write, initialized
    ]
    at = 0x80 + 24 + 0xE0
    for name, address, size, flags in sections:
        image[at:at + 8] = name.ljust(8, b"\0")
        struct.pack_into("<II", image, at + 8, size, address)  # VirtualSize, VirtualAddress
        struct.pack_into("<I", image, at + 36, flags)
        at += 40
    # .pdata: one function at the entry point, length in instructions (big-endian, as the guest).
    # Bits 30-31 are the entry type: 1 = a normal function without an exception handler
    # (0 would mark a register-save millicode helper; see ReXGlue tests/code_image.h).
    struct.pack_into(">II", image, PDATA_OFFSET, LOAD_ADDRESS + TEXT_OFFSET, (len(CODE) << 8) | (1 << 30))
    # .text: the program (big-endian).
    for i, word in enumerate(CODE):
        struct.pack_into(">I", image, TEXT_OFFSET + 4 * i, word)
    return bytes(image)


def xex(image: bytes) -> bytes:
    def be32(value):
        return struct.pack(">I", value)

    file_format = bytearray(8)
    struct.pack_into(">IHH", file_format, 0, 8, 0, 0)  # size 8, encryption none, compression none
    execution = bytearray(0x18)
    struct.pack_into(">I", execution, 0xC, TITLE_ID)
    entries = [
        (0x000003FF, bytes(file_format), 0),           # file format info
        (0x00010100, b"", LOAD_ADDRESS + TEXT_OFFSET),  # entry point
        (0x00040006, bytes(execution), 0),             # execution info
    ]
    header = bytearray(0x18 + 8 * len(entries))
    header[0:4] = b"XEX2"
    struct.pack_into(">I", header, 0x4, 0x00000001)    # module flags: title
    struct.pack_into(">I", header, 0x14, len(entries))
    for i, (key, blob, value) in enumerate(entries):
        struct.pack_into(">I", header, 0x18 + 8 * i, key)
        if blob:
            while len(header) % 4:
                header.append(0)
            struct.pack_into(">I", header, 0x18 + 8 * i + 4, len(header))
            header += blob
        else:
            struct.pack_into(">I", header, 0x18 + 8 * i + 4, value)
    while len(header) % 4:
        header.append(0)
    security = len(header)
    struct.pack_into(">I", header, 0x10, security)
    header += bytes(0x184 + 0x18)
    struct.pack_into(">I", header, security + 0x4, IMAGE_SIZE)       # image size (one 64 KiB page)
    struct.pack_into(">I", header, security + 0x10C, 0)              # image flags: 64 KiB pages
    struct.pack_into(">I", header, security + 0x110, LOAD_ADDRESS)   # load address
    struct.pack_into(">I", header, security + 0x180, 1)              # one page descriptor
    struct.pack_into(">I", header, security + 0x184, (1 << 4) | 2)   # 1 page, data (read write)
    struct.pack_into(">I", header, 0x8, len(header))                 # PE data offset
    return bytes(header) + image


def main() -> int:
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "default.xex"), "wb") as f:
        f.write(xex(pe_image()))
    with open(os.path.join(out, "testgame_manifest.toml"), "w", encoding="utf-8") as f:
        f.write('[project]\nname = "testgame"\n\n[entrypoint]\nfile_path = "default.xex"\n'
                'out_directory_path = "generated/default"\n')
    print("wrote", os.path.join(out, "default.xex"), "and testgame_manifest.toml")
    return 0


if __name__ == "__main__":
    sys.exit(main())

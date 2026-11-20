#!/usr/bin/env python3
"""Writes the project's own tiny Xbox 360 test program as an unencrypted, uncompressed XEX, plus
the ReXGlue manifest to recompile it. No game code: every instruction below is this project's.

Part 1 (shell 0.1.4) checks recompiled code: sum 1..50,000,000 in a loop.
Part 2 (shell 0.1.5) calls the Xbox 360 kernel the way a game does, through imports of
xboxkrnl.exe: it reads the performance-counter frequency and the system time, starts a second
guest thread, waits for it, and closes its handle. The second thread doubles the value it is
given and leaves a marker.

Results (big-endian words in guest memory, read by App/HostBridge/RexHeadlessHost.cpp):
    0x82004000  sum mod 2^32              0x82004004  marker 0x600DF00D
    0x82004010  performance frequency     0x82004018  system time (64-bit FILETIME)
    0x82004020  thread handle             0x82004024  ExCreateThread status
    0x82004028  wait status               0x82004030  thread result (0x1234 * 2)
    0x82004034  thread marker 0x7EAD

The container follows ReXGlue's own unit-test images (tests/code_image.cpp,
tests/unit/base/xex_builder.h, tests/unit/kernel/cross_module_import_test.cpp): a 64 KiB PE image
at 0x82000000 in a XEX2 with no encryption and no compression. Nothing is encrypted, so nothing
is decrypted.

usage: make_test_xex.py <output folder>
writes <output folder>/default.xex and <output folder>/testgame_manifest.toml
"""
import os
import struct
import sys

LOAD_ADDRESS = 0x82000000
IMAGE_SIZE = 0x10000
TEXT_OFFSET, PDATA_OFFSET, IMPORT_OFFSET, DATA_OFFSET = 0x1000, 0x2000, 0x3000, 0x4000
TITLE_ID = 0x7E570001  # a made-up test title ID, not any real game's

# xboxkrnl.exe exports used (ordinals from ReXGlue src/kernel/xboxkrnl/export_table.inc).
IMPORTS = [
    ("KeQueryPerformanceFrequency", 0x83),
    ("KeQuerySystemTime", 0x84),
    ("ExCreateThread", 0x0D),
    ("NtWaitForSingleObjectEx", 0xFD),
    ("NtClose", 0xCF),
]


def slot_address(index):   # the import's variable slot: holds the ordinal
    return LOAD_ADDRESS + IMPORT_OFFSET + 0x20 * index


def thunk_address(index):  # the import's 16-byte call thunk, filled by the loader
    return slot_address(index) + 0x10


def thunk(name):
    return thunk_address([n for n, _ in IMPORTS].index(name))


# --- a minimal PowerPC assembler for the instructions used ---
def d_form(op, rt, ra, imm):
    return (op << 26) | (rt << 21) | (ra << 16) | (imm & 0xFFFF)


def li(rt, value):     return d_form(14, rt, 0, value)       # addi rt, 0, value
def addi(rt, ra, v):   return d_form(14, rt, ra, v)
def lis(rt, value):    return d_form(15, rt, 0, value)       # addis rt, 0, value
def ori(ra, rs, v):    return d_form(24, rs, ra, v)
def stw(rs, d, ra):    return d_form(36, rs, ra, d)
def stwu(rs, d, ra):   return d_form(37, rs, ra, d)
def lwz(rt, d, ra):    return d_form(32, rt, ra, d)
def add(rt, ra, rb):   return (31 << 26) | (rt << 21) | (ra << 16) | (rb << 11) | (266 << 1)
def cmpw(ra, rb):      return (31 << 26) | (ra << 16) | (rb << 11)
MFLR_R0 = 0x7C0802A6
MTLR_R0 = 0x7C0803A6
BLR = 0x4E800020


class Code:
    def __init__(self, address):
        self.address = address
        self.words = []
        self.labels = {}

    def pc(self):
        return self.address + 4 * len(self.words)

    def emit(self, *words):
        self.words.extend(words)

    def label(self, name):
        self.labels[name] = self.pc()

    def bl(self, target):  # branch and link, relative
        self.emit(0x48000001 | ((target - self.pc()) & 0x03FFFFFC))

    def ble(self, name):   # bc 4, cr0.gt: branch if not greater
        self.emit((16 << 26) | (4 << 21) | (1 << 16) | ((self.labels[name] - self.pc()) & 0xFFFC))

    def address_of(self, reg, address):
        self.emit(lis(reg, address >> 16), ori(reg, reg, address & 0xFFFF))


def program():
    text = LOAD_ADDRESS + TEXT_OFFSET
    data = LOAD_ADDRESS + DATA_OFFSET
    code = Code(text)
    # main (the entry point)
    code.emit(MFLR_R0, stw(0, -8, 1), stwu(1, -0x70, 1))
    code.emit(li(3, 0), li(4, 1), lis(5, 0x02FA), ori(5, 5, 0xF080))  # sum = 0, i = 1, n = 50e6
    code.label("loop")
    code.emit(add(3, 3, 4), addi(4, 4, 1), cmpw(4, 5))
    code.ble("loop")
    code.emit(lis(11, data >> 16), stw(3, 0x4000, 11), lis(7, 0x600D), ori(7, 7, 0xF00D), stw(7, 0x4004, 11))
    code.bl(thunk("KeQueryPerformanceFrequency"))
    code.emit(lis(11, data >> 16), stw(3, 0x4010, 11))
    code.address_of(3, data + 0x18)
    code.bl(thunk("KeQuerySystemTime"))
    code.address_of(3, data + 0x20)                       # r3 = &handle
    code.emit(li(4, 0), li(5, 0), li(6, 0))                # stack size, thread id, xapi startup
    thread_entry_index = len(code.words)
    code.emit(0, 0)                                        # r7 = thread start, patched below
    code.emit(li(8, 0x1234), li(9, 0))                     # context, creation flags
    code.bl(thunk("ExCreateThread"))
    code.emit(lis(11, data >> 16), stw(3, 0x4024, 11))
    code.emit(lwz(3, 0x4020, 11), li(4, 1), li(5, 0), li(6, 0))  # handle, user mode, no alert, no timeout
    code.bl(thunk("NtWaitForSingleObjectEx"))
    code.emit(lis(11, data >> 16), stw(3, 0x4028, 11), lwz(3, 0x4020, 11))
    code.bl(thunk("NtClose"))
    code.emit(addi(1, 1, 0x70), lwz(0, -8, 1), MTLR_R0, BLR)
    main_length = len(code.words)
    # the second thread: r3 = context
    thread = code.pc()
    code.emit(add(4, 3, 3), lis(11, data >> 16), stw(4, 0x4030, 11), li(5, 0x7EAD), stw(5, 0x4034, 11),
              li(3, 0), BLR)
    code.words[thread_entry_index:thread_entry_index + 2] = [lis(7, thread >> 16), ori(7, 7, thread & 0xFFFF)]
    functions = [(text, main_length), (thread, len(code.words) - main_length)]
    return code.words, functions


def pe_image(words, functions):
    image = bytearray(IMAGE_SIZE)
    struct.pack_into("<HH", image, 0, 0x5A4D, 0x90)  # 'MZ', e_cblp
    struct.pack_into("<I", image, 0x3C, 0x80)         # e_lfanew
    image[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", image, 0x84, 0x01F2, 3, 0, 0, 0, 0xE0, 0x0100)  # POWERPCBE
    opt = 0x98
    struct.pack_into("<H", image, opt + 0, 0x10B)
    struct.pack_into("<I", image, opt + 60, TEXT_OFFSET)  # SizeOfHeaders
    struct.pack_into("<H", image, opt + 68, 14)           # Subsystem: Xbox
    struct.pack_into("<II", image, opt + 96 + 3 * 8, PDATA_OFFSET, 8 * len(functions))
    sections = [
        (b".text", TEXT_OFFSET, max(0x200, 4 * len(words)), 0x20000000),  # execute
        (b".pdata", PDATA_OFFSET, 0x100, 0x40000040),                     # read
        (b".data", DATA_OFFSET, 0x100, 0xC0000040),                       # read write
    ]
    at = 0x80 + 24 + 0xE0
    for name, address, size, flags in sections:
        image[at:at + 8] = name.ljust(8, b"\0")
        struct.pack_into("<II", image, at + 8, size, address)
        struct.pack_into("<I", image, at + 36, flags)
        at += 40
    # .pdata: start and length in instructions; bits 30-31 = 1, a normal function without an
    # exception handler (0 would mark a register-save millicode helper; ReXGlue tests/code_image.h).
    for i, (start, length) in enumerate(functions):
        struct.pack_into(">II", image, PDATA_OFFSET + 8 * i, start, (length << 8) | (1 << 30))
    for i, word in enumerate(words):
        struct.pack_into(">I", image, TEXT_OFFSET + 4 * i, word)
    # Import slots and thunks: the slot holds the ordinal, the thunk (1 << 24) | ordinal.
    for i, (_, ordinal) in enumerate(IMPORTS):
        struct.pack_into(">I", image, slot_address(i) - LOAD_ADDRESS, ordinal)
        struct.pack_into(">I", image, thunk_address(i) - LOAD_ADDRESS, (1 << 24) | ordinal)
    return bytes(image)


def import_libraries():
    names = b"xboxkrnl.exe\0"
    while len(names) % 4:
        names += b"\0"
    records = []
    for i in range(len(IMPORTS)):
        records += [slot_address(i), thunk_address(i)]
    library = bytearray(0x28)
    struct.pack_into(">I", library, 0, 0x28 + 4 * len(records))
    struct.pack_into(">HH", library, 0x24, 0, len(records))  # library index, record count
    library += b"".join(struct.pack(">I", r) for r in records)
    table = struct.pack(">III", 0, len(names), 1) + names + bytes(library)
    return struct.pack(">I", len(table)) + table[4:]


def xex(image):
    file_format = struct.pack(">IHH", 8, 0, 0)  # size 8, encryption none, compression none
    execution = bytearray(0x18)
    struct.pack_into(">I", execution, 0xC, TITLE_ID)
    entries = [
        (0x000003FF, file_format, 0),                   # file format info
        (0x00010100, b"", LOAD_ADDRESS + TEXT_OFFSET),  # entry point
        (0x00040006, bytes(execution), 0),             # execution info
        (0x000103FF, import_libraries(), 0),           # import libraries
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
    struct.pack_into(">I", header, security + 0x128, 1)              # import table count
    struct.pack_into(">I", header, security + 0x180, 1)              # one page descriptor
    struct.pack_into(">I", header, security + 0x184, (1 << 4) | 2)   # 1 page, data (read write)
    struct.pack_into(">I", header, 0x8, len(header))                 # PE data offset
    return bytes(header) + image


def main() -> int:
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    words, functions = program()
    with open(os.path.join(out, "default.xex"), "wb") as f:
        f.write(xex(pe_image(words, functions)))
    with open(os.path.join(out, "testgame_manifest.toml"), "w", encoding="utf-8") as f:
        f.write('[project]\nname = "testgame"\n\n[entrypoint]\nfile_path = "default.xex"\n'
                'out_directory_path = "generated/default"\n')
    print("wrote", os.path.join(out, "default.xex"), "and testgame_manifest.toml:",
          len(words), "instructions,", len(functions), "functions,", len(IMPORTS), "kernel imports")
    for i, w in enumerate(words):
        print(f"  {LOAD_ADDRESS + TEXT_OFFSET + 4 * i:08X}  {w:08X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

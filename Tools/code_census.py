#!/usr/bin/env python3
"""Read-only size census of an x86-32 PE's code, without a disassembler.

Usage:  python -I Tools/code_census.py <path-to-exe>

Gives rough, heuristic figures used only to estimate the scale of a static
recompilation or decompilation effort. It counts byte patterns; it does not
disassemble, execute or modify anything. Every number is an estimate.
"""
import re
import struct
import sys


def sections(d):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opt = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    out = {}
    off = pe + 24 + opt
    for i in range(nsec):
        name, vsize, va, rsize, rptr = struct.unpack_from("<8sIIII", d, off + i * 40)
        out[name.rstrip(b"\0").decode("latin-1")] = (va, vsize, rptr, rsize)
    return base, out


def main():
    with open(sys.argv[1], "rb") as f:  # read-only
        d = f.read()
    base, secs = sections(d)
    va, vsize, rptr, rsize = secs[".text"]
    text = d[rptr:rptr + rsize]
    lo, hi = base + va, base + va + vsize
    print("image base 0x%X, .text 0x%X..0x%X (%d bytes)" % (base, lo, hi, len(text)))
    print("sections:", ", ".join(secs))

    # Function-start heuristics.
    frame = len(re.findall(rb"\x55\x8b\xec", text))                       # push ebp; mov ebp, esp
    after_pad = len(re.findall(rb"(?:\xcc|\x90){1,15}(?=[\x55\x53\x56\x57\x51\x83\x8b\x6a\xa1\xe9\x81\xb8\x33\xc7\xf3\xd9\x0f])", text))
    aligned = sum(1 for m in re.finditer(rb"\xcc+", text) if (m.end() % 16) == 0)
    rets = len(re.findall(rb"\xc3|\xc2..", text, re.S))
    print("\nfunction-start estimates")
    print("  'push ebp; mov ebp,esp' prologues      %d" % frame)
    print("  code starts following int3/nop padding  %d" % after_pad)
    print("  int3 padding runs ending on 16-byte boundary %d" % aligned)
    print("  ret / ret imm16 byte patterns (upper bound, includes data) %d" % rets)

    # Direct call targets: E8 rel32 landing inside .text. Unique targets approximate called functions.
    targets = set()
    for m in re.finditer(rb"\xe8", text):
        i = m.start()
        if i + 5 > len(text):
            break
        rel = struct.unpack_from("<i", text, i + 1)[0]
        t = lo + i + 5 + rel
        if lo <= t < hi:
            targets.add(t)
    print("  unique in-.text targets of E8 call patterns %d (includes false matches)" % len(targets))

    # Pointers into .text stored in data sections: vtables, callbacks, jump tables.
    ptrs = 0
    code_ptr_targets = set()
    for name in (".rdata", ".data"):
        sva, svs, sp, sr = secs[name]
        blob = d[sp:sp + sr]
        for i in range(0, len(blob) - 3, 4):
            v = struct.unpack_from("<I", blob, i)[0]
            if lo <= v < hi:
                ptrs += 1
                code_ptr_targets.add(v)
    print("\nindirect-control-flow indicators")
    print("  4-byte values in .rdata/.data pointing into .text  %d (%d unique)" % (ptrs, len(code_ptr_targets)))
    print("  'call [abs32]' (FF 15) patterns   %d" % len(re.findall(rb"\xff\x15", text)))
    print("  'call reg' (FF D0-D7) patterns    %d" % len(re.findall(rb"\xff[\xd0-\xd7]", text)))
    print("  'call [reg+disp]' (FF 50-57 / 90-97) patterns %d" % len(re.findall(rb"\xff[\x50-\x57\x90-\x97]", text)))
    print("  'jmp [abs32+reg*4]' (FF 24 85/8D/95/9D/B5/BD) switch patterns %d"
          % len(re.findall(rb"\xff\x24[\x85\x8d\x95\x9d\xb5\xbd]", text)))

    # Instruction-set indicators (byte patterns; approximate).
    print("\ninstruction-set indicators (approximate)")
    print("  x87 escape opcodes D8-DF followed by a memory/reg modrm  %d" % len(re.findall(rb"[\xd8-\xdf][\x00-\xff]", text)))
    print("  SSE scalar prefixes F3 0F / F2 0F   %d" % len(re.findall(rb"[\xf2\xf3]\x0f", text)))
    print("  SSE packed 0F 28/29/58/59/5C/5E     %d" % len(re.findall(rb"\x0f[\x28\x29\x58\x59\x5c\x5e]", text)))
    print("  66 0F (SSE2 integer/double)         %d" % len(re.findall(rb"\x66\x0f", text)))
    print("  lock prefix (F0) before cmpxchg/xadd/inc/dec  %d" % len(re.findall(rb"\xf0\x0f[\xb0\xb1\xc0\xc1]", text)))
    print("  fs:[...] TEB access (64 A1 / 64 8B / 64 89 / 64 A3)  %d" % len(re.findall(rb"\x64[\xa1\xa3\x8b\x89]", text)))
    print("  cpuid (0F A2)  %d   rdtsc (0F 31)  %d" % (len(re.findall(rb"\x0f\xa2", text)), len(re.findall(rb"\x0f\x31", text))))


if __name__ == "__main__":
    main()

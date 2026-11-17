#!/usr/bin/env python3
"""Read-only static PE inspector (standard library only).

Usage:  python -I Tools/pe_inspect.py <path-to-exe-or-dll> [--json out.json] [--strings]

Opens the target in binary read mode only. Never executes, maps or modifies it.
Reports: headers, sections (with entropy), data directories, imports, delay
imports, exports, TLS, debug/PDB records, Authenticode presence and, with
--strings, a keyword census of embedded ASCII/UTF-16 strings.
"""
import argparse
import hashlib
import json
import math
import re
import struct
import sys
from collections import Counter, OrderedDict

MACHINES = {0x14C: "x86 (I386, 32-bit)", 0x8664: "x86-64 (AMD64)", 0xAA64: "ARM64", 0x1C0: "ARM", 0x1C4: "ARMNT"}
SUBSYSTEMS = {1: "NATIVE", 2: "WINDOWS_GUI", 3: "WINDOWS_CUI", 9: "WINDOWS_CE_GUI", 14: "XBOX"}
DIR_NAMES = ["EXPORT", "IMPORT", "RESOURCE", "EXCEPTION", "SECURITY", "BASERELOC", "DEBUG", "ARCHITECTURE",
             "GLOBALPTR", "TLS", "LOAD_CONFIG", "BOUND_IMPORT", "IAT", "DELAY_IMPORT", "COM_DESCRIPTOR", "RESERVED"]
DLL_CHARS = {0x0020: "HIGH_ENTROPY_VA", 0x0040: "DYNAMIC_BASE(ASLR)", 0x0080: "FORCE_INTEGRITY", 0x0100: "NX_COMPAT(DEP)",
             0x0200: "NO_ISOLATION", 0x0400: "NO_SEH", 0x0800: "NO_BIND", 0x1000: "APPCONTAINER", 0x2000: "WDM_DRIVER",
             0x4000: "GUARD_CF", 0x8000: "TERMINAL_SERVER_AWARE"}
FILE_CHARS = {0x0001: "RELOCS_STRIPPED", 0x0002: "EXECUTABLE_IMAGE", 0x0020: "LARGE_ADDRESS_AWARE", 0x0100: "32BIT_MACHINE",
              0x0200: "DEBUG_STRIPPED", 0x2000: "DLL"}
SEC_CHARS = {0x20: "CODE", 0x40: "INIT_DATA", 0x80: "UNINIT_DATA", 0x20000000: "X", 0x40000000: "R", 0x80000000: "W"}


def entropy(b):
    if not b:
        return 0.0
    c = Counter(b)
    n = len(b)
    return -sum(v / n * math.log2(v / n) for v in c.values())


def flags(v, table):
    return [name for bit, name in table.items() if v & bit]


class PE:
    def __init__(self, path):
        with open(path, "rb") as f:  # read-only
            self.d = f.read()
        d = self.d
        if d[:2] != b"MZ":
            raise ValueError("not an MZ executable")
        self.e_lfanew = struct.unpack_from("<I", d, 0x3C)[0]
        if d[self.e_lfanew:self.e_lfanew + 4] != b"PE\0\0":
            raise ValueError("PE signature missing")
        o = self.e_lfanew + 4
        (self.machine, self.nsec, self.timestamp, _, _, self.opt_size, self.fchars) = struct.unpack_from("<HHIIIHH", d, o)
        o += 20
        self.opt_off = o
        self.magic = struct.unpack_from("<H", d, o)[0]
        self.pe64 = self.magic == 0x20B
        self.linker = struct.unpack_from("<BB", d, o + 2)
        self.entry = struct.unpack_from("<I", d, o + 16)[0]
        if self.pe64:
            self.image_base = struct.unpack_from("<Q", d, o + 24)[0]
            base = o + 24 + 8
        else:
            self.image_base = struct.unpack_from("<I", d, o + 28)[0]
            base = o + 28 + 4
        (self.sec_align, self.file_align, osmaj, osmin, imaj, imin, ssmaj, ssmin, _, self.size_image, self.size_headers,
         self.checksum, self.subsystem, self.dllchars) = struct.unpack_from("<IIHHHHHHIIIIHH", d, base)
        self.os_ver = (osmaj, osmin)
        self.subsys_ver = (ssmaj, ssmin)
        ptr = "<QQQQ" if self.pe64 else "<IIII"
        after = base + struct.calcsize("<IIHHHHHHIIIIHH")
        (self.stack_reserve, self.stack_commit, self.heap_reserve, self.heap_commit) = struct.unpack_from(ptr, d, after)
        after += struct.calcsize(ptr)
        _, ndirs = struct.unpack_from("<II", d, after)
        after += 8
        self.dirs = OrderedDict()
        for i in range(min(ndirs, 16)):
            rva, size = struct.unpack_from("<II", d, after + i * 8)
            self.dirs[DIR_NAMES[i]] = (rva, size)
        so = self.opt_off + self.opt_size
        self.sections = []
        for i in range(self.nsec):
            name, vsize, va, rsize, rptr, _, _, _, _, chars = struct.unpack_from("<8sIIIIIIHHI", d, so + i * 40)
            self.sections.append(dict(name=name.rstrip(b"\0").decode("latin-1"), vsize=vsize, va=va, rsize=rsize,
                                      rptr=rptr, chars=chars))
        self.overlay_off = max([s["rptr"] + s["rsize"] for s in self.sections] + [0])

    def rva2off(self, rva):
        for s in self.sections:
            if s["va"] <= rva < s["va"] + max(s["vsize"], s["rsize"]):
                off = rva - s["va"] + s["rptr"]
                return off if off < len(self.d) and (rva - s["va"]) < s["rsize"] else None
        return None

    def cstr(self, rva, limit=512):
        off = self.rva2off(rva)
        if off is None:
            return None
        end = self.d.find(b"\0", off, off + limit)
        return self.d[off:end if end != -1 else off + limit].decode("latin-1")

    def _thunks(self, rva):
        out = []
        off = self.rva2off(rva)
        if off is None:
            return out
        size, fmt, ordflag = (8, "<Q", 1 << 63) if self.pe64 else (4, "<I", 1 << 31)
        while off + size <= len(self.d):
            v = struct.unpack_from(fmt, self.d, off)[0]
            if v == 0:
                break
            if v & ordflag:
                out.append("#%d" % (v & 0xFFFF))
            else:
                n = self.cstr((v & 0x7FFFFFFF) + 2)
                out.append(n if n is not None else "<unresolved rva 0x%X>" % v)
            off += size
            if len(out) > 20000:
                break
        return out

    def imports(self):
        res = OrderedDict()
        rva, size = self.dirs.get("IMPORT", (0, 0))
        off = self.rva2off(rva) if rva else None
        if off is None:
            return res
        while True:
            oft, _, _, name_rva, ft = struct.unpack_from("<IIIII", self.d, off)
            if not (oft or name_rva or ft):
                break
            name = self.cstr(name_rva) or "<bad name>"
            res.setdefault(name, []).extend(self._thunks(oft or ft))
            off += 20
        return res

    def delay_imports(self):
        res = OrderedDict()
        rva, size = self.dirs.get("DELAY_IMPORT", (0, 0))
        off = self.rva2off(rva) if rva else None
        if off is None:
            return res
        while True:
            attrs, name_rva, _, _, int_rva, _, _, _ = struct.unpack_from("<IIIIIIII", self.d, off)
            if not (name_rva or int_rva):
                break
            adj = 0 if (attrs & 1) else self.image_base  # old-style entries hold VAs
            name = self.cstr(name_rva - adj) or "<bad name>"
            res.setdefault(name, []).extend(self._thunks(int_rva - adj))
            off += 32
        return res

    def exports(self):
        rva, size = self.dirs.get("EXPORT", (0, 0))
        off = self.rva2off(rva) if rva else None
        if off is None:
            return None
        (_, _, _, _, name_rva, base, nfuncs, nnames, _, names_rva, _) = struct.unpack_from("<IIHHIIIIIII", self.d, off)
        names = []
        noff = self.rva2off(names_rva)
        if noff is not None:
            for i in range(min(nnames, 5000)):
                names.append(self.cstr(struct.unpack_from("<I", self.d, noff + i * 4)[0]))
        return dict(dll_name=self.cstr(name_rva), functions=nfuncs, names=names)

    def debug(self):
        out = []
        rva, size = self.dirs.get("DEBUG", (0, 0))
        off = self.rva2off(rva) if rva else None
        if off is None:
            return out
        for i in range(size // 28):
            _, ts, _, _, typ, dsize, _, dptr = struct.unpack_from("<IIHHIIII", self.d, off + i * 28)
            e = dict(type=typ, timestamp=ts, size=dsize)
            if typ == 2 and self.d[dptr:dptr + 4] == b"RSDS":
                end = self.d.find(b"\0", dptr + 24)
                e["pdb"] = self.d[dptr + 24:end].decode("latin-1")
            out.append(e)
        return out

    def tls(self):
        rva, size = self.dirs.get("TLS", (0, 0))
        off = self.rva2off(rva) if rva else None
        if off is None:
            return None
        fmt = "<QQQQ" if self.pe64 else "<IIII"
        start, end, index, callbacks = struct.unpack_from(fmt, self.d, off)
        cbs = []
        if callbacks:
            coff = self.rva2off(callbacks - self.image_base)
            psz, pf = (8, "<Q") if self.pe64 else (4, "<I")
            while coff is not None and len(cbs) < 64:
                v = struct.unpack_from(pf, self.d, coff)[0]
                if not v:
                    break
                cbs.append(hex(v))
                coff += psz
        return dict(raw_data_bytes=end - start, callbacks=cbs)


KEYWORDS = [
    # graphics
    "D3D11CreateDevice", "D3D11", "D3D10", "Direct3DCreate9", "d3d9", "dxgi", "DXGI", "D3DCompile", "d3dcompiler",
    "d3dx", "D3DX", "OpenGL", "opengl32", "vs_5_0", "ps_5_0", "vs_4_0", "ps_4_0", "vs_3_0", "ps_3_0", "DXBC", "hlsl",
    "Tessellation", "HullShader", "ComputeShader", "nvapi", "atiadl", "amd_ags",
    # audio
    "XAudio2", "xaudio2", "X3DAudio", "DirectSound", "dsound", "MMDevAPI", "WASAPI", "XMA", "EALayer3", "rw::audio",
    "SndPlayer", "Vorbis", "Speex",
    # input
    "XInput", "xinput", "DirectInput", "dinput8", "RawInput", "GetRawInputData", "Logitech",
    # video
    "VP6", "On2", "Bink", "bink", "TrueMotion",
    # engine / middleware
    "Criterion", "Chameleon", "RenderWare", "rw::", "renderengine", "BrnWorld", "BrnGame", "Brn", "Burnout", "CgsCore",
    "Cgs", "Havok", "hkp", "Scaleform", "GFx", "Autolog", "Blaze", "DirtySDK", "DirtySock", "ProtoSSL", "EASTL",
    "EA::", "EAThread", "Kinect", "Lua", "lua_", "zlib", "inflate", "deflate", "LZX", "Oodle", "lz4",
    # DRM / online
    "Origin", "EACore", "Activation.dll", "SecuROM", "Denuvo", "VMProtect", "Themida", "Steam", "steam_api", "Solidshield",
    # data
    ".BNDL", "BNDL", "bnd2", "BND2", ".sps", ".SPS", ".vp6", "STARDATA", "WORLDLIST", "HAWAII",
    # platform strings
    "Xbox", "PS3", "PlayStation", "Vita", "WiiU", "Win32", "x64", "ARM",
]


def strings_census(d, minlen=5):
    ascii_re = re.compile(rb"[\x20-\x7e]{%d,}" % minlen)
    wide_re = re.compile(rb"(?:[\x20-\x7e]\x00){%d,}" % minlen)
    strs = [m.group().decode("latin-1") for m in ascii_re.finditer(d)]
    strs += [m.group().decode("utf-16le") for m in wide_re.finditer(d)]
    hits = OrderedDict()
    for kw in KEYWORDS:
        matched = [s for s in strs if kw in s]
        if matched:
            uniq = list(OrderedDict.fromkeys(matched))
            hits[kw] = dict(count=len(matched), samples=[s[:140] for s in uniq[:12]])
    return len(strs), hits, strs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--json")
    ap.add_argument("--strings", action="store_true")
    ap.add_argument("--dump-strings", help="write every extracted string to this file")
    ap.add_argument("--brief", action="store_true", help="headers + import DLL names only")
    a = ap.parse_args()
    pe = PE(a.path)
    d = pe.d
    import datetime
    rep = OrderedDict()
    rep["file"] = a.path
    rep["size"] = len(d)
    rep["sha256"] = hashlib.sha256(d).hexdigest()
    rep["machine"] = MACHINES.get(pe.machine, hex(pe.machine))
    rep["format"] = "PE32+ (64-bit)" if pe.pe64 else "PE32 (32-bit)"
    rep["link_timestamp_utc"] = datetime.datetime.fromtimestamp(pe.timestamp, datetime.timezone.utc).isoformat()
    rep["linker_version"] = "%d.%d" % pe.linker
    rep["subsystem"] = SUBSYSTEMS.get(pe.subsystem, pe.subsystem)
    rep["os_version_min"] = "%d.%d" % pe.os_ver
    rep["subsystem_version"] = "%d.%d" % pe.subsys_ver
    rep["image_base"] = hex(pe.image_base)
    rep["entry_point_rva"] = hex(pe.entry)
    rep["size_of_image"] = pe.size_image
    rep["stack_reserve"] = pe.stack_reserve
    rep["file_characteristics"] = flags(pe.fchars, FILE_CHARS)
    rep["dll_characteristics"] = flags(pe.dllchars, DLL_CHARS)
    ep_sec = next((s["name"] for s in pe.sections if s["va"] <= pe.entry < s["va"] + max(s["vsize"], s["rsize"])), None)
    rep["entry_point_section"] = ep_sec
    rep["sections"] = [dict(name=s["name"], vaddr=hex(s["va"]), vsize=s["vsize"], rawsize=s["rsize"],
                            flags="".join(sorted(f for f in flags(s["chars"], SEC_CHARS) if len(f) == 1)),
                            kind=[f for f in flags(s["chars"], SEC_CHARS) if len(f) > 1],
                            entropy=round(entropy(d[s["rptr"]:s["rptr"] + s["rsize"]]), 3)) for s in pe.sections]
    rep["overlay_bytes"] = max(0, len(d) - pe.overlay_off)
    rep["data_directories"] = {k: dict(rva=hex(v[0]), size=v[1]) for k, v in pe.dirs.items() if v[0] or v[1]}
    rep["authenticode_signature_present"] = bool(pe.dirs.get("SECURITY", (0, 0))[1])
    imps = pe.imports()
    rep["imports"] = imps
    rep["delay_imports"] = pe.delay_imports()
    rep["exports"] = pe.exports()
    rep["debug"] = pe.debug()
    rep["tls"] = pe.tls()
    if a.strings or a.dump_strings:
        n, hits, strs = strings_census(d)
        rep["string_count"] = n
        rep["keyword_hits"] = hits
        if a.dump_strings:
            with open(a.dump_strings, "w", encoding="utf-8", errors="replace") as f:
                f.write("\n".join(strs))
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(rep, f, indent=1)

    def p(*x):
        sys.stdout.write(" ".join(str(i) for i in x) + "\n")

    for k in ["file", "size", "sha256", "machine", "format", "link_timestamp_utc", "linker_version", "subsystem",
              "os_version_min", "subsystem_version", "image_base", "entry_point_rva", "entry_point_section",
              "size_of_image", "stack_reserve", "file_characteristics", "dll_characteristics", "overlay_bytes",
              "authenticode_signature_present", "debug", "tls"]:
        p("%-32s %s" % (k, rep[k]))
    p("\nSECTIONS")
    for s in rep["sections"]:
        p("  %-9s va=%-10s vsize=%-9d raw=%-9d %-4s H=%.3f %s" % (s["name"], s["vaddr"], s["vsize"], s["rawsize"],
                                                             s["flags"], s["entropy"], ",".join(s["kind"])))
    p("\nDATA DIRECTORIES", json.dumps(rep["data_directories"]))
    for title, table in (("IMPORTS", imps), ("DELAY IMPORTS", rep["delay_imports"])):
        p("\n%s (%d DLLs, %d symbols)" % (title, len(table), sum(len(v) for v in table.values())))
        for dll, fns in table.items():
            if a.brief:
                p("  %-28s %d" % (dll, len(fns)))
            else:
                p("  %s (%d): %s" % (dll, len(fns), ", ".join(fns)))
    if rep["exports"]:
        p("\nEXPORTS", rep["exports"]["dll_name"], rep["exports"]["functions"], rep["exports"]["names"][:40])
    if a.strings:
        p("\nSTRING KEYWORD CENSUS (%d strings)" % rep["string_count"])
        for kw, h in rep["keyword_hits"].items():
            p("  [%s] x%d: %s" % (kw, h["count"], " | ".join(h["samples"][:6])))


if __name__ == "__main__":
    main()

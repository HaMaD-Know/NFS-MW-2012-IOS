#!/usr/bin/env python3
"""Packs the "runtime kit" (Option A, stage A1): what a Linux machine needs to compile ReXGlue-
generated code for the iPad, without Apple's SDK and without any game code.

The kit holds:
  flags.txt     the compiler flags ReXGlue's iOS libraries were built with (one per line), with
                include folders pointing into the kit (@KIT@/inc/N) and Apple-SDK and target
                options removed (the Linux side adds its own target and headers)
  inc/N/        copies of those include folders (headers only)
  frameworks/   ReXGlue's iOS libraries (librex*.dylib), to link against
  stub/         ReXGlue's allocator stub source, linked into every module library
  testgame/     the project's own test program's generated code (no game content), for testing

usage: make_runtime_kit.py <ReXGlue iOS build dir> <rexglue-sdk dir> <frameworks dir>
                           <test program generated dir> <kit dir>
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_host_bridge import base_flags  # noqa: E402

HEADER_EXT = (".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".inc", ".def")


def copy_headers(src, dst):
    count = 0
    for root, _dirs, files in os.walk(src):
        for name in files:
            if name.endswith(HEADER_EXT):
                rel = os.path.relpath(os.path.join(root, name), src)
                os.makedirs(os.path.dirname(os.path.join(dst, rel)), exist_ok=True)
                shutil.copy2(os.path.join(root, name), os.path.join(dst, rel))
                count += 1
    return count


def main() -> int:
    build_dir, sdk_dir, frameworks, testgame, kit = (os.path.abspath(a) for a in sys.argv[1:6])
    if os.path.exists(kit):
        shutil.rmtree(kit)
    os.makedirs(kit)
    flags, cwd = base_flags(build_dir)
    flags = flags[1:]  # the compiler itself
    out, folders = [], {}
    i = 0
    while i < len(flags):
        a = flags[i]
        if a in ("-isysroot", "-arch", "-target", "--target"):
            i += 2
            continue
        if a.startswith(("-miphoneos-version-min", "-mios-version-min", "--sysroot", "-isysroot")):
            i += 1
            continue
        for opt in ("-I", "-isystem", "-iquote"):
            if a == opt or (a.startswith(opt) and opt == "-I" and len(a) > 2):
                path = flags[i + 1] if a == opt else a[2:]
                path = os.path.normpath(os.path.join(cwd, path))
                if path not in folders:
                    folders[path] = len(folders)
                    n = copy_headers(path, os.path.join(kit, "inc", str(folders[path])))
                    print(f"inc/{folders[path]}: {n} headers from {path}")
                out += [opt if opt != "-I" else "-I", f"@KIT@/inc/{folders[path]}"]
                i += 2 if a == opt else 1
                break
        else:
            out.append(a)
            i += 1
    with open(os.path.join(kit, "flags.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print("flags:", " ".join(out))
    os.makedirs(os.path.join(kit, "frameworks"))
    for name in sorted(os.listdir(frameworks)):
        if name.startswith("librex") and name.endswith(".dylib"):
            shutil.copy2(os.path.join(frameworks, name), os.path.join(kit, "frameworks", name))
    os.makedirs(os.path.join(kit, "stub"))
    shutil.copy2(os.path.join(sdk_dir, "cmake", "rexglue_host_alloc_stub.cpp"), os.path.join(kit, "stub"))
    shutil.copytree(testgame, os.path.join(kit, "testgame"))
    print("frameworks:", " ".join(sorted(os.listdir(os.path.join(kit, "frameworks")))))
    return 0


if __name__ == "__main__":
    sys.exit(main())

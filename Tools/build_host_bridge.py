#!/usr/bin/env python3
"""Compiles sources for the iOS host bridge with exactly the compiler flags ReXGlue's own app
library (src/app/app.cpp) was built with, read from the build's compile_commands.json.

Same flags means the same defines, include folders, language standard and target, so the bridge
sees ReXGlue's classes exactly as the libraries do. The precompiled header, dependency-file
options, the output and input and the library's private REX_BUILDING_* define are left out.

With --recomp, the sources are ReXGlue-generated recompiled code: warnings are not errors (the
generated code is not ours to change), and ReXGlue's recompiled-code options are added
(rexglue_recomp_options: -fasynchronous-unwind-tables; -O2 here instead of its default -O1) together with the generated
precompiled header, which the generated files expect to be included first.

Used by .github/workflows/ipad-runtime-app.yml on the macOS runner.

usage: build_host_bridge.py <build dir> <object dir> [--include DIR]... [--recomp PCH] source...
"""
import json
import os
import shlex
import subprocess
import sys


def base_flags(build_dir):
    with open(os.path.join(build_dir, "compile_commands.json"), encoding="utf-8") as f:
        commands = json.load(f)
    entry = next(c for c in commands if c["file"].replace("\\", "/").endswith("src/app/app.cpp"))
    args = entry.get("arguments") or shlex.split(entry["command"])
    out = [args[0]]
    i = 1
    while i < len(args):
        a = args[i]
        if a == "-Xclang" and i + 1 < len(args) and args[i + 1] in ("-include-pch", "-include"):
            i += 4  # -Xclang -include-pch -Xclang <file>
            continue
        if a == "-include" and i + 1 < len(args) and "cmake_pch" in args[i + 1]:
            i += 2
            continue
        if a in ("-o", "-c", "-MF", "-MT", "-MQ", "--serialize-diagnostics"):
            i += 2
            continue
        if a in ("-MD", "-MMD") or a.startswith("-DREX_BUILDING_") or a == entry["file"]:
            i += 1
            continue
        if a.endswith(("app.cpp", ".pch")) or "cmake_pch" in a:
            i += 1
            continue
        out.append(a)
        i += 1
    return out, entry["directory"]


def main() -> int:
    build_dir, object_dir = (os.path.abspath(a) for a in sys.argv[1:3])
    rest = sys.argv[3:]
    includes, sources, pch = [], [], None
    i = 0
    while i < len(rest):
        if rest[i] == "--include":
            includes.append(os.path.abspath(rest[i + 1]))
            i += 2
        elif rest[i] == "--recomp":
            pch = os.path.abspath(rest[i + 1])
            i += 2
        else:
            sources.append(os.path.abspath(rest[i]))
            i += 1
    flags, cwd = base_flags(build_dir)
    if pch:
        # Generated code: keep ReXGlue's defines and includes, drop warning flags and -O0.
        flags = [f for f in flags if not (f.startswith("-W") or f in ("-O0", "-Werror"))]
        # ReXGlue's default for recompiled code is -O1; shell 0.1.5 measures -O2 on the iPad.
        flags += ["-O2", "-fasynchronous-unwind-tables", "-w", "-include", pch]
    flags += [f"-I{d}" for d in includes]
    os.makedirs(object_dir, exist_ok=True)
    print("Compiler flags:", " ".join(shlex.quote(a) for a in flags), flush=True)
    # Compile in parallel, one job per CPU core. An object compiled from the same source contents
    # (and the same headers in the --include folders) with the same flags is kept, so a restored cache
    # of game objects is not compiled again, and a cache from an older translation is never reused
    # for changed sources. File times are no help: restored and downloaded files get new times.
    import concurrent.futures, glob, hashlib
    base = hashlib.sha256(" ".join(flags).encode())
    for header in sorted(set(([pch] if pch else [])
                             + [h for d in includes for h in glob.glob(os.path.join(d, "*.h"))])):
        with open(header, "rb") as f:
            base.update(os.path.basename(header).encode() + b"\0" + f.read())
    def compile_one(source):
        obj = os.path.join(object_dir, os.path.splitext(os.path.basename(source))[0] + ".o")
        h = base.copy()
        with open(source, "rb") as f:
            h.update(f.read())
        mark = obj + "." + h.hexdigest()[:16]
        if os.path.exists(obj) and os.path.exists(mark):
            return source, 0, "kept"
        for old in glob.glob(glob.escape(obj) + ".*"):
            os.remove(old)
        proc = subprocess.run(flags + ["-c", source, "-o", obj], cwd=cwd, capture_output=True, text=True)
        if proc.returncode == 0:
            open(mark, "w").close()
        return source, proc.returncode, (proc.stdout + proc.stderr)[-4000:]
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
        for source, status, output in pool.map(compile_one, sources):
            print(("Kept " if output == "kept" else "Compiled ") + os.path.basename(source)
                  + ("" if status == 0 else " FAILED"), flush=True)
            if status != 0:
                print(output, flush=True)
                failed = status
    return failed


if __name__ == "__main__":
    sys.exit(main())

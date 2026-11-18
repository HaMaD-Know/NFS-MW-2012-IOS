#!/usr/bin/env python3
"""Compiles App/HostBridge/RexHeadlessHost.cpp with exactly the compiler flags ReXGlue's own app
library (src/app/app.cpp) was built with, read from the build's compile_commands.json.

Same flags means the same defines, include folders, language standard and target, so the bridge
sees ReXGlue's classes exactly as the libraries do. The precompiled header, dependency-file
options, the output and input and the library's private REX_BUILDING_* define are left out.

Used by .github/workflows/ipad-runtime-app.yml on the macOS runner.

usage: build_host_bridge.py <build dir> <source.cpp> <object.o>
"""
import json
import os
import shlex
import subprocess
import sys


def main() -> int:
    build_dir, source, obj = (os.path.abspath(a) for a in sys.argv[1:4])
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

    command = out + ["-c", source, "-o", obj]
    print("Compiling the host bridge with ReXGlue's app flags:")
    print(" ".join(shlex.quote(a) for a in command), flush=True)
    return subprocess.call(command, cwd=entry["directory"])


if __name__ == "__main__":
    sys.exit(main())

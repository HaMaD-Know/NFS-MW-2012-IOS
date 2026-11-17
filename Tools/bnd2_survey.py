#!/usr/bin/env python3
"""Read-only survey of Criterion "Bundle 2" (bnd2) archives in a game directory.

Usage:  python -I Tools/bnd2_survey.py <game-dir> [--json out.json]

Only reads headers and resource-entry tables (plus one small resource per
bundle flag-set to confirm the compression method). Extracts nothing to disk
and never writes inside <game-dir>.

Header layout below was derived empirically from the files and is validated
on every bundle by the check:
    resource_entries_offset + count * ENTRY_SIZE == data_offset[0]
"""
import argparse
import collections
import json
import os
import struct
import sys
import zlib

ENTRY_SIZE = 0x48
HEADER_FMT = "<4sHHIII4II"  # magic, version, platform, debug_off, count, entries_off, data_off[4], flags
ENTRY_FMT = "<Q4I4I4IIIHBB"  # id, uncompressed[4], on_disk[4], disk_off[4], import_off, type, import_count, flags, stream

PLATFORMS = {1: "PC (little-endian)", 2: "Xbox 360", 3: "PS3"}


def survey(game):
    per_type = collections.defaultdict(lambda: dict(count=0, uncompressed=0, on_disk=0, bundles=set()))
    versions = collections.Counter()
    platforms = collections.Counter()
    flagsets = collections.Counter()
    layout_ok = layout_bad = 0
    bad_files = []
    zprobe = collections.Counter()
    bundles = 0
    resources = 0
    top_dirs = collections.defaultdict(lambda: collections.Counter())
    for root, _, files in os.walk(game):
        for fn in files:
            if not fn.lower().endswith(".bndl"):
                continue
            path = os.path.join(root, fn)
            rel = os.path.relpath(path, game)
            top = rel.split(os.sep)[0] if os.sep in rel else "(root)"
            with open(path, "rb") as f:  # read-only
                head = f.read(struct.calcsize(HEADER_FMT))
                if len(head) < struct.calcsize(HEADER_FMT) or head[:4] != b"bnd2":
                    bad_files.append(rel)
                    continue
                (magic, ver, plat, dbg, count, ent_off, d0, d1, d2, d3, flags) = struct.unpack(HEADER_FMT, head)
                bundles += 1
                versions[ver] += 1
                platforms[plat] += 1
                flagsets[flags] += 1
                if ent_off + count * ENTRY_SIZE == d0:
                    layout_ok += 1
                else:
                    layout_bad += 1
                    bad_files.append(rel)
                    continue
                f.seek(ent_off)
                table = f.read(count * ENTRY_SIZE)
                probed = False
                for i in range(count):
                    e = struct.unpack_from(ENTRY_FMT, table, i * ENTRY_SIZE)
                    rid = e[0]
                    unc = [x & 0x0FFFFFFF for x in e[1:5]]
                    dsk = [x & 0x0FFFFFFF for x in e[5:9]]
                    offs = e[9:13]
                    rtype = e[14]
                    t = per_type[rtype]
                    t["count"] += 1
                    t["uncompressed"] += sum(unc)
                    t["on_disk"] += sum(dsk)
                    t["bundles"].add(top)
                    top_dirs[top][rtype] += 1
                    resources += 1
                    if not probed and 0 < dsk[0] <= 1 << 20:
                        probed = True
                        f.seek(d0 + offs[0])
                        blob = f.read(dsk[0])
                        if flags & 1:
                            try:
                                out = zlib.decompress(blob)
                                zprobe["zlib ok, size matches" if len(out) == unc[0] else "zlib ok, size differs"] += 1
                            except zlib.error:
                                zprobe["flag=compressed but zlib failed"] += 1
                        else:
                            zprobe["stored (flag bit0 clear)"] += 1
    return dict(
        bundles=bundles, resources=resources, versions=dict(versions),
        platforms={PLATFORMS.get(k, str(k)): v for k, v in platforms.items()},
        header_flag_sets={hex(k): v for k, v in sorted(flagsets.items())},
        layout_check=dict(ok=layout_ok, failed=layout_bad), unparsed_files=bad_files[:50],
        compression_probe=dict(zprobe),
        resource_types={"0x%X" % k: dict(count=v["count"], uncompressed_bytes=v["uncompressed"],
                                         on_disk_bytes=v["on_disk"], found_in=sorted(v["bundles"]))
                        for k, v in sorted(per_type.items())},
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game")
    ap.add_argument("--json")
    a = ap.parse_args()
    rep = survey(a.game)
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(rep, f, indent=1)
    w = sys.stdout.write
    for k in ("bundles", "resources", "versions", "platforms", "header_flag_sets", "layout_check", "compression_probe"):
        w("%-18s %s\n" % (k, rep[k]))
    if rep["unparsed_files"]:
        w("unparsed: %s\n" % rep["unparsed_files"])
    w("\n%-8s %8s %12s %12s  %s\n" % ("type", "count", "uncomp MB", "disk MB", "found in"))
    for k, v in rep["resource_types"].items():
        w("%-8s %8d %12.1f %12.1f  %s\n" % (k, v["count"], v["uncompressed_bytes"] / 1048576, v["on_disk_bytes"] / 1048576,
                                         ",".join(v["found_in"])))


if __name__ == "__main__":
    main()

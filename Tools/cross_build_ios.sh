#!/usr/bin/env bash
# Option A (Documentation: LINUX_GAME_BUILD): compiles ReXGlue-generated code into an iPad (arm64
# iOS) library on Linux, without Apple's SDK.
#
#   compiler  LLVM clang, target arm64-apple-ios17.0
#   C++ lib   LLVM libc++ headers (the same ABI, std::__1, as Apple's libc++)
#   C lib     Zig's bundled macOS headers (macOS and iOS on arm64 share the C library ABI)
#   linker    ld64.lld, against ReXGlue's iOS libraries from the runtime kit, and against small
#             stub files (.tbd) written here that list only the libc++ and libSystem symbols used
#   signing   rcodesign, ad hoc (the sideloading tool signs again at install)
#
# usage: cross_build_ios.sh <runtime kit> <generated dir> <project prefix> <output .dylib> <work dir>
#   e.g. cross_build_ios.sh kit kit/testgame testgame out/libtestgame.dylib work
# Needs: CLANG, LLD (ld64.lld), LLVM_BIN (llvm tools), LIBCXX_INC, LIBCXX_SITE (folder with
# __config_site), DARWIN_LIBC (space-separated macOS libc header folders), RCODESIGN.
set -euo pipefail
export LC_ALL=C

kit=$(realpath "$1"); gen=$(realpath "$2"); prefix=$3; out=$(realpath -m "$4"); work=$(realpath -m "$5")
mkdir -p "$work/obj" "$(dirname "$out")"

# --- Flags: ReXGlue's own flags (from the macOS build), the iOS target and the open headers.
mapfile -t base < <(sed "s|@KIT@|$kit|g" "$kit/flags.txt" | grep -v '^$')
target=(--target=arm64-apple-ios17.0 -nostdinc++ -nostdlibinc -isystem "$LIBCXX_INC" -isystem "$LIBCXX_SITE")
for d in $DARWIN_LIBC; do target+=(-idirafter "$d"); done
# Generated code (as Tools/build_host_bridge.py --recomp): no warnings, -O2, unwind tables, the PCH.
recomp=()
for f in "${base[@]}"; do
  case "$f" in -W*|-O0|-Werror) ;; *) recomp+=("$f") ;; esac
done
recomp+=(-O2 -fasynchronous-unwind-tables -w -I "$gen" -include "$gen/${prefix}_pch.h")
echo "clang: $("$CLANG" --version | head -1)"
echo "flags: ${target[*]} ${recomp[*]}"

# --- Compile, one job per core. Each object is kept when its source and flags are unchanged.
flagsum=$(printf '%s\n' "${target[@]}" "${recomp[@]}" | cat - "$gen"/*.h | sha256sum | cut -c1-16)
compile() {  # source object flags...
  local src=$1 obj=$2; shift 2
  local mark="$obj.$(cat "$src" | sha256sum | cut -c1-16)-$flagsum"
  [ -f "$obj" ] && [ -f "$mark" ] && return 0
  rm -f "$obj".*
  "$CLANG" "$@" -c "$src" -o "$obj" && touch "$mark"
}
export -f compile; export CLANG flagsum
start=$(date +%s)
printf '%s\n' "$gen"/*.cpp | xargs -P "$(nproc)" -I{} bash -c \
  'compile "$1" "$0/obj/$(basename "${1%.cpp}").o" "${@:2}"' "$work" {} "${target[@]}" "${recomp[@]}"
compile "$kit/stub/rexglue_host_alloc_stub.cpp" "$work/obj/zz_alloc_stub.o" "${target[@]}" "${base[@]}"
# EXTRA_SOURCES: our own files compiled like the generated code (e.g. Tools/game_image_shim.cpp).
for extra in ${EXTRA_SOURCES:-}; do
  compile "$extra" "$work/obj/zz_$(basename "${extra%.cpp}").o" "${target[@]}" "${recomp[@]}"
done
echo "compiled $(ls "$work"/obj/*.o | wc -l) objects in $(( $(date +%s) - start )) s"

# --- Stub libraries for the system symbols the code uses (names only, written here).
nm="$LLVM_BIN/llvm-nm"
"$nm" -gU --format=just-symbols "$work"/obj/*.o 2>/dev/null | sort -u > "$work/defined.txt"
"$nm" -gU --format=just-symbols "$kit"/frameworks/*.dylib 2>/dev/null | sort -u > "$work/rex.txt"
"$nm" -u --format=just-symbols "$work"/obj/*.o 2>/dev/null | sort -u \
  | comm -23 - "$work/defined.txt" | comm -23 - "$work/rex.txt" > "$work/system.txt"
grep -E '^(__ZN?K?St3__1|__ZNSt|__ZNKSt|__ZTVNSt|__ZTINSt|__ZTSNSt|__ZTISt|__ZTVSt|__ZSt|__Znwm|__Znam|__ZdlPv|__ZdaPv|__ZnwmSt|__ZnamSt|__ZdlPvSt|__ZdaPvSt|___cxa_|___gxx_personality|__ZTVN10__cxxabiv1|__ZTIN10__cxxabiv1|___dynamic_cast)' \
  "$work/system.txt" > "$work/cxx.txt" || true
comm -23 "$work/system.txt" "$work/cxx.txt" > "$work/libsystem.txt"
tbd() {  # install-name symbol-file out
  { echo '--- !tapi-tbd'; echo 'tbd-version: 4'; echo 'targets: [ arm64-ios ]'
    echo "install-name: '$1'"; echo 'exports:'; echo '  - targets: [ arm64-ios ]'
    echo -n '    symbols: [ '; paste -sd, "$2" | sed 's/,/, /g' | tr -d '\n'; echo ' ]'; echo '...'; } > "$3"
}
tbd /usr/lib/libc++.1.dylib "$work/cxx.txt" "$work/libc++.tbd"
tbd /usr/lib/libSystem.B.dylib "$work/libsystem.txt" "$work/libSystem.tbd"
echo "system symbols: $(wc -l < "$work/cxx.txt") from libc++, $(wc -l < "$work/libsystem.txt") from libSystem"

# --- Link, sign, check.
"$LLD" -arch arm64 -platform_version ios 17.0 17.0 -dylib \
  -install_name "@rpath/$(basename "$out")" -rpath @loader_path -dead_strip_dylibs \
  -o "$out" "$work"/obj/*.o "$kit"/frameworks/librex*.dylib "$work/libc++.tbd" "$work/libSystem.tbd"
"$RCODESIGN" sign "$out" > "$work/sign.txt" 2>&1 || { cat "$work/sign.txt"; exit 1; }
echo "--- $(basename "$out"): $(du -h "$out" | cut -f1)"
"$LLVM_BIN/llvm-objdump" --macho --private-headers "$out" | grep -A4 LC_BUILD_VERSION | head -5
"$LLVM_BIN/llvm-otool" -L "$out" 2>/dev/null || "$LLVM_BIN/llvm-objdump" --macho --dylibs-used "$out"
echo "imported symbols: $("$nm" -u "$out" | wc -l)"

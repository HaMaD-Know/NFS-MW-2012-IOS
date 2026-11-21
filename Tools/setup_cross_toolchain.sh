#!/usr/bin/env bash
# Installs the open-source tools Tools/cross_build_ios.sh needs on an Ubuntu 24.04 runner and
# writes their paths to $GITHUB_ENV (or prints them). No Apple SDK is used.
#   LLVM 20 (apt.llvm.org): clang, ld64.lld, llvm tools, libc++ headers (ReXGlue's headers need
#   std::atomic_ref, which libc++ has since LLVM 19)
#   Zig 0.13.0 (ziglang.org): only its bundled macOS C library headers
#   rcodesign (apple-codesign, GitHub release): ad hoc signing
set -euo pipefail
tools=${1:-$HOME/cross-tools}; mkdir -p "$tools"
env_out=${GITHUB_ENV:-/dev/stdout}

v=20
curl -fsSL --retry 5 --retry-all-errors -o "$tools/llvm.sh" https://apt.llvm.org/llvm.sh
sudo bash "$tools/llvm.sh" $v > "$tools/llvm-install.txt" 2>&1 || { tail -30 "$tools/llvm-install.txt"; exit 1; }
sudo apt-get install -y -qq libc++-$v-dev libc++abi-$v-dev > /dev/null

zig=zig-linux-x86_64-0.13.0
curl -fsSL --retry 5 --retry-all-errors -o "$tools/zig.tar.xz" "https://ziglang.org/download/0.13.0/$zig.tar.xz"
tar -xf "$tools/zig.tar.xz" -C "$tools"
libc="$tools/$zig/lib/libc/include"
echo "Zig's macOS header folders:"; ls -d "$libc"/*macos* | sed 's|.*/||'
# Zig 0.13 has one folder for every macOS version and CPU; later versions may split it.
darwin="$(ls -d "$libc"/aarch64-macos* 2>/dev/null | sort -V | tail -1) $(ls -d "$libc"/any-macos* | sort -V | tail -1)"

rc=apple-codesign-0.29.0-x86_64-unknown-linux-musl
curl -fsSL --retry 5 --retry-all-errors -o "$tools/rc.tar.gz" \
  "https://github.com/indygreg/apple-platform-rs/releases/download/apple-codesign%2F0.29.0/$rc.tar.gz"
tar -xzf "$tools/rc.tar.gz" -C "$tools"

site=$(dirname "$(find /usr/lib/llvm-$v /usr/include -name __config_site 2>/dev/null | head -1)")
{
  echo "CLANG=/usr/lib/llvm-$v/bin/clang++"
  echo "LLD=/usr/lib/llvm-$v/bin/ld64.lld"
  echo "LLVM_BIN=/usr/lib/llvm-$v/bin"
  echo "LIBCXX_INC=/usr/lib/llvm-$v/include/c++/v1"
  echo "LIBCXX_SITE=$site"
  echo "DARWIN_LIBC=$darwin"
  echo "RCODESIGN=$tools/$rc/rcodesign"
} | tee -a "$env_out"

#!/usr/bin/env bash
# Installs the open-source tools Tools/cross_build_ios.sh needs on an Ubuntu 24.04 runner and
# writes their paths to $GITHUB_ENV (or prints them). No Apple SDK is used.
#   LLVM 18 (Ubuntu packages): clang, ld64.lld, llvm tools, libc++ headers
#   Zig 0.13.0 (ziglang.org): only its bundled macOS C library headers
#   rcodesign (apple-codesign, GitHub release): ad hoc signing
set -euo pipefail
tools=${1:-$HOME/cross-tools}; mkdir -p "$tools"
env_out=${GITHUB_ENV:-/dev/stdout}

sudo apt-get update -qq
sudo apt-get install -y -qq clang-18 lld-18 llvm-18 libc++-18-dev libc++abi-18-dev > /dev/null

zig=zig-linux-x86_64-0.13.0
curl -fsSL --retry 5 --retry-all-errors -o "$tools/zig.tar.xz" "https://ziglang.org/download/0.13.0/$zig.tar.xz"
tar -xf "$tools/zig.tar.xz" -C "$tools"
libc="$tools/$zig/lib/libc/include"
echo "Zig's macOS header folders:"; ls -d "$libc"/*macos* | sed 's|.*/||'
# The newest arm64 macOS folder first, then the shared ones.
darwin="$(ls -d "$libc"/aarch64-macos* | sort -V | tail -1) $(ls -d "$libc"/any-macos* | sort -V | tail -1)"

rc=apple-codesign-0.29.0-x86_64-unknown-linux-musl
curl -fsSL --retry 5 --retry-all-errors -o "$tools/rc.tar.gz" \
  "https://github.com/indygreg/apple-platform-rs/releases/download/apple-codesign%2F0.29.0/$rc.tar.gz"
tar -xzf "$tools/rc.tar.gz" -C "$tools"

site=$(dirname "$(find /usr/lib/llvm-18 /usr/include -name __config_site 2>/dev/null | head -1)")
{
  echo "CLANG=/usr/lib/llvm-18/bin/clang++"
  echo "LLD=/usr/lib/llvm-18/bin/ld64.lld"
  echo "LLVM_BIN=/usr/lib/llvm-18/bin"
  echo "LIBCXX_INC=/usr/lib/llvm-18/include/c++/v1"
  echo "LIBCXX_SITE=$site"
  echo "DARWIN_LIBC=$darwin"
  echo "RCODESIGN=$tools/$rc/rcodesign"
} | tee -a "$env_out"

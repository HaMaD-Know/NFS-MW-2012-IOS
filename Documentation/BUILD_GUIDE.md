# Build Guide

## 1. iPadOS test shell (no Mac needed)

The shell is built only in GitHub Actions (`.github/workflows/ios-shell.yml`):

1. `xcodegen generate` turns `App/project.yml` into an Xcode project on the macOS runner.
2. `xcodebuild -sdk iphoneos -configuration Release` builds it for arm64 with code signing
   disabled.
3. The workflow checks that the binary is arm64 and built for iOS, zips `Payload/` into
   `NFSMWShell-unsigned.ipa`, and uploads it with its SHA-256.

Run it manually with *Actions › iPad test shell › Run workflow*. It also runs on every push
that changes `App/`.

**Signing:** the IPA is unsigned. Sign it on Windows with your own Apple ID through a
sideloading tool. A free Apple ID signs for 7 days and allows three sideloaded apps. No
certificate, key or provisioning profile is stored in this repository or in CI.

**Shaders:** the shell compiles its Metal shader from source on the device at run time, so no
Apple Metal compiler is needed at build time. The overlay shows how long that takes.

## 2. ReXGlue SDK

### In CI

`.github/workflows/rexglue-sdk.yml` builds ReXGlue at a pinned commit with its unit tests, on
`windows-latest` and `macos-26`, using ReXGlue's own toolchain action. The run summary
records build time and disk use. Start it with *Actions › ReXGlue SDK › Run workflow*.

### On a Windows PC

From ReXGlue's Getting Started guide:

- Visual Studio 2026 with **Desktop development with C++** and **C++ Clang Compiler for
  Windows**; Clang 20 or newer is required.
- CMake and Ninja.
- LunarG Vulkan SDK 1.4.313 or newer.

```
git clone --recursive https://github.com/rexglue/rexglue-sdk.git
cd rexglue-sdk
cmake --preset win-amd64
cmake --build out/build/win-amd64 --target install
```

Notes for a PC with 8 GB of RAM: build with `--parallel 2` (or 1 if the build runs out of
memory), and keep the source and build tree on a drive with plenty of free space. Visual
Studio and the Windows SDK place several GB on the system drive even when installed
elsewhere.

## 3. Never

- Never put a game file, disc image, title update or generated recompiler output in this
  repository, in a CI run, or in a release artifact.

# N1-b — Native iPadOS ARM64 Support for ReXGlue: Feasibility and Plan

Written 2026-11-17. This is a desk assessment. **Nothing in it has been built or run for
iOS yet.**

## Progress log

| Date | Step | Result | Evidence |
|---|---|---|---|
| 2026-11-17 | B1 trial, unpatched | Configure fails with one error: `install TARGETS given no BUNDLE DESTINATION for MACOSX_BUNDLE executable target "rexglue"` (on iOS the recompiler CLI becomes an app bundle). Vulkan SDK, Iconv and all submodules are found for iOS | Run 44113373357 |
| 2026-11-17 | B3 patches 0001–0002 | Configure passes; 905/925 build steps; 2 files fail: `mach_vm_region` (`<mach/mach_vm.h>` is unsupported in the iOS SDK) in `memory_posix.cpp` and `process_resources_posix.cpp` | Run 44114620284 |
| 2026-11-17 | B3 patch 0003 | **925/925 steps, 0 compiler errors, 0 failed steps.** Every runtime module links as an iOS arm64 library. Linker warnings only: duplicate `libxxhashd.a` on the link line; `-undefined error` deprecated | Run 44116653662 |
| 2026-11-17 | B2 self-test | Added to the iPad shell; builds | Run 44112115204 |
| 2026-11-17 | **B0 + B2 on the device** | iPad Pro 11" M2 (`iPad14,3`), iPadOS 26.6.1, free Apple ID. Shell: Metal "Apple M2 GPU", 2388 × 1668, runtime shader compile 214.6 ms, Xbox Wireless Controller connected, **5.00 GB** app memory at launch. Self-test, 8 identical runs: **Q1 address space PASS** (largest reservation 5.00 GB, total reservable 6 GB); **guest memory FAIL**: `shm_open` → EPERM; **Q4 fault handling not determined** (setup used `shm_open`); **Q3 fibers FAIL**: `getcontext` → ENOTSUP | Device log |

### Device findings and their consequences (2026-11-17)

| # | Finding | Consequence | Next action |
|---|---|---|---|
| D1 | `shm_open` is not permitted in the app sandbox | ReXGlue's guest memory (`CreateFileMappingHandle` → `shm_open`, then nine `MAP_SHARED` views) cannot work on iOS as written. **Blocker for T3.** This contradicts the MW 2005 mobile port's report that `shm_open` works on iOS (unverified; it may use a different path or entitlement) | Test two replacements on the device: (a) anonymous memory with mirror views made by Mach `vm_remap` (`copy = FALSE`); (b) a file in the app's tmp folder, unlinked after opening, mapped `MAP_SHARED` several times. Pick by correctness, then by performance |
| D2 | `getcontext` returns ENOTSUP | ucontext fibers do not exist on iOS. **Blocker for T4** | Replace `fiber_posix.cpp`'s switch on iOS with a small arm64 context switch (callee-saved x19–x28, fp, lr, sp, d8–d15) |
| D3 | Fault handling untested | Needed for MMIO and GPU write-watching | Re-run on top of the chosen D1 backing |
| D4 | Total reservable address space is 6 GB | The 4.5 GB guest block fits, but leaves ~1.5 GB for MoltenVK, Metal and the heaps of a full app | Measure again once the runtime starts (B7). Fallback: the extended-virtual-addressing entitlement, which needs a paid developer account |
| D5 | 5.00 GB of memory available under a free Apple ID | Better than expected; enough headroom for a 512 MB-RAM console game plus the runtime | None now |

Patch series (`patches/rexglue-ios/`, applied to `ebac1cf`):
- `0001-ios-platform-identity`: `REX_PLATFORM_IOS`, the `ios-arm64` platform, iOS 17 deployment
  target, no `MAP_JIT` or executable pages on iOS, no `fork`/`execv` relaunch, and the instance
  lock in `TMPDIR`.
- `0002-ios-skip-host-tools`: the recompiler CLI is excluded from the default iOS build and not
  installed.
- `0003-ios-vm-region`: `vm_region_64` instead of `mach_vm_region` on iOS.

Of the predicted tasks in §3:
- **Done:** T1, T2, T5, T6 and T12 (CLI excluded).
- **Turned out to need no change to compile:** T11. SDL3, FFmpeg, glslang and the other
  submodules built for iOS as they are.
- **Compile-time task not predicted, now done:** `mach_vm_region` (patch 0003).
- **Still open, all run-time problems that compiling cannot reveal:**
  - T3: guest memory reservation on the device.
  - T4: fibers.
  - T7: storage paths.
  - T8: `dlopen` of the module libraries from inside the app bundle.
  - T9: the Vulkan loader. The code still `dlopen`s `libvulkan.1.dylib`, which does not exist on
    iOS; MoltenVK must be embedded.
  - T10: thread priority.
  - T13: app lifecycle.

## 0. Starting point (verified)

| Item | State | Evidence |
|---|---|---|
| Repository | `HaMaD-Know/NFS-MW-2012-IOS`, `main` at `cf4c1bf`; local copy identical to `origin/main` | `git fetch`, `git rev-list` |
| iPad test shell | Builds as an unsigned arm64 iOS IPA in CI; **not yet run on the iPad** | Actions run 44043947553 |
| ReXGlue SDK at `ebac1cf` | Builds from source with tests on Windows x64 (2,222 tests, 0 failed, 3 skipped) and macOS arm64 (2,219 passed, 2 skipped, 2 timing tests retried) | Actions run 44069785573 |
| ReXGlue on iOS | **Never attempted** | — |
| The game | **Not analysed.** No Xbox 360 executable has been examined, and nothing shows it recompiles | — |

**Method.** I read the ReXGlue source at the pinned commit `ebac1cf` (1,081 files, from the
GitHub source archive). I also read the published SDK patches and notes of the two existing
iOS ports (NFSMW-Recompiled-Mobile and Theft4) for reference only. They are GPL-3.0, and no
code was copied.

## 1. Short answer

Porting ReXGlue's runtime to iPadOS looks **feasible**. Nothing found is a hard, unsolvable
blocker:

- the runtime needs no JIT;
- it has no Objective-C or AppKit code;
- its Apple support already goes through SDL3 and Vulkan with a Metal surface;
- upstream already contains Android code paths, which face similar mobile constraints.

There are **about a dozen concrete porting tasks** (§3), and **four questions that only a run
on the real iPad can answer** (§4). The most important is whether iPadOS lets the app reserve
the roughly 4.5 GB block of virtual address space that the runtime maps for the Xbox 360
memory. Comparable ports report that it fits; this project has not measured it.

N1-b ends with the runtime *starting* on iPadOS. It does not run a game, and it does not
produce native Metal graphics: stage 1 uses Vulkan on MoltenVK, as decided in
`ARCHITECTURE.md` R4.

## 2. What already works in our favour

| Finding | Where | Why it matters |
|---|---|---|
| **No executable memory is ever needed for guest code.** Guest pages that request execute access hit an assertion (`FromPageAccess` → `REX_ASSERT_ALWAYS`); the game's code is already native at build time | `src/kernel/memory.cpp` | iPadOS forbids JIT for sideloaded apps without a debugger. This runtime doesn't need it |
| `include/rex/platform.h` selects macOS when `TARGET_OS_MAC` is set — and the iOS SDK sets `TARGET_OS_MAC=1` too | `include/rex/platform.h` | An iOS build compiles the POSIX/Apple paths (mach clocks, 16 KB page handling) automatically. It also compiles macOS-only assumptions, which must be guarded (§3) |
| No Objective-C, no AppKit, no `.mm` files | whole SDK | Nothing to rewrite for UIKit inside the SDK |
| Windowing, input and audio go through **SDL3**, which supports iOS | `src/ui/sdl3`, `src/hid/sdl3`, `src/apu/sdl3` | Controllers through SDL3's GameController backend; audio through CoreAudio |
| The Vulkan backend already speaks **MoltenVK**: `VK_EXT_metal_surface`, `CAMetalLayer` surfaces, `VK_KHR_portability_subset` | `src/gpu/xenos/host/vulkan/*`, `surface.h` | The macOS GPU path is the iOS GPU path, with a different library-loading step |
| Fault handling for MMIO and write-watching uses ordinary POSIX signals (`SIGSEGV`, `SIGBUS`) | `src/base/exception_handler_posix.cpp` | Signal handlers work in iOS apps when no debugger is attached |
| Upstream already has **Android** branches (25 uses of `REX_PLATFORM_ANDROID`) | memory, threading | Mobile concerns (shared memory, thread naming) are already handled once; iOS can follow the same pattern |
| Every runtime library is built from an `OBJECT` library | `src/*/CMakeLists.txt` | Static or bundled-dylib variants can be produced without restructuring sources |
| BSD-3-Clause licence | `LICENSE` | Our iOS patches can be kept permissive and offered upstream |

## 3. Porting tasks found in the source

| # | Area | What the code does now | Problem on iPadOS | Planned change | Size |
|---|---|---|---|---|---|
| T1 | Platform identity | `TARGET_OS_MAC` → `REX_PLATFORM_MAC`; CMake maps every Apple build to `mac-arm64`/`mac-amd64`, and fails for other systems | macOS-only paths compile for iOS; no `ios-arm64` platform or preset exists | Add `REX_PLATFORM_IOS` (from `TARGET_OS_IPHONE`) alongside `REX_PLATFORM_MAC`; add an `ios-arm64` platform string, a CMake toolchain or preset (`CMAKE_SYSTEM_NAME=iOS`, `iphoneos`, deployment target 17+), and guard macOS-only code | Small |
| T2 | Executable memory flags | `AllocFixed` adds `MAP_JIT` and `PROT_EXEC` when execute access is requested | `MAP_JIT` fails without the JIT entitlement | On iOS, refuse execute access outright with a clear error. Guest memory never asks for it (§2); the guard proves it at run time | Small |
| T3 | Guest memory | One shared-memory object of **0x120000000 bytes (4.5 GB)** via `shm_open` + `ftruncate`, mapped as several views (physical mirrors at 0xA0000000, 0xC0000000, 0xE0000000), placed with `MAP_FIXED` (`MapViewsMac`) | Must fit within iPadOS's per-app **virtual address** limit. `shm_open` inside the app sandbox must work. This reserves address space, not RAM | Keep the design; measure it on the device first (§4 Q1). Fallbacks if refused: the extended-virtual-addressing entitlement (paid account), or a file-backed mapping in the app container instead of `shm_open` | **Unknown until measured** |
| T4 | Fibers | `getcontext`/`makecontext`/`swapcontext` (deprecated on Darwin; warnings silenced) | Behaviour on iOS arm64 is unverified | Test on the device. If broken, replace with a small arm64 context switch, a few dozen lines of assembly, which is a well-known technique | Small–medium |
| T5 | Relaunch | `relaunch_posix.cpp` uses `fork()` + `execv()` | iOS apps cannot fork or spawn processes | Compile out on iOS; restarts are handled by the app shell | Small |
| T6 | Single-instance lock | Lock file in `/tmp` | Outside the sandbox on iOS | Use `TMPDIR` or the app's caches folder | Small |
| T7 | Storage paths | `HOME`, `XDG_DATA_HOME`, `XDG_CONFIG_HOME` | XDG variables don't exist; game data must come from a folder the user picks | iOS paths: `Documents` for saves and logs, `Library/Caches` for shader caches, and the BYOG game-data location — either `Application Support/GameData/` or a security-scoped bookmark — passed in from Swift as the runtime's game root (`BYOG_DESIGN.md` §5) | Small–medium |
| T8 | Runtime modules | Every system (`rexbase`, `rexkernel`, `rexgpu`, `rexapu`, `rexhid`, `rexui`, `rexfilesystem`, …) is a **shared library loaded with `dlopen`**; each exports the same C entry points (`ReXCreateInterface`, `ReXBuildIdentity`) | Loose libraries outside the bundle cannot load. Linking all modules statically clashes on the identical symbol names | **Option A:** ship the modules as signed dylibs inside `App.app/Frameworks`, which iOS allows and sideloading tools re-sign. **Option B:** give each module a unique entry name and register modules statically. Start with A; move to B if load times or signing problems appear | Medium |
| T9 | Vulkan loader | `dlopen("libvulkan.1.dylib")` on Apple | iOS has no system Vulkan loader | Link **MoltenVK** (from the LunarG SDK's iOS xcframework) into the app and take `vkGetInstanceProcAddr` from it directly | Small–medium |
| T10 | Thread scheduling | `pthread_setschedparam(SCHED_FIFO)` (already only warns on failure); affinity calls are Linux-only | Real-time scheduling isn't available to apps | Map priorities to iOS QoS classes; keep the warning path | Small |
| T11 | Third-party libraries | SDL3, FFmpeg (XMA audio decoding, `wmarti/FFmpeg` fork), glslang, spdlog, Tracy, … built from submodules | Each must cross-compile for iOS arm64. FFmpeg needs its iOS configure; Tracy is profiling only | Build FFmpeg for iOS (decoder-only, LGPL configuration); turn Tracy off on iOS; check every other submodule in the CI probe | Medium |
| T12 | Codegen tool | `rexglue` (the recompiler) is a host tool | It must keep running on Windows or macOS, not on iOS | iOS builds the *runtime* only. Codegen stays on the host, unchanged | None |
| T13 | App integration | SDL3 creates the window and Metal layer | It has to sit inside our Swift app's lifecycle (background, suspend, memory warnings) | Start with SDL3's UIKit integration; hand lifecycle events from Swift to the runtime | Medium |

## 4. Questions only the iPad can answer

The GitHub macOS runners can build for iOS and run the **iOS Simulator**. The simulator does
not enforce a real device's memory, address-space and code-signing limits, so these need the
iPad:

| # | Question | Why it decides things | How we measure it |
|---|---|---|---|
| Q1 | Can the app reserve and map the **4.5 GB guest memory block** plus its mirror views under a free Apple ID on an M2 with iPadOS 26? | If not, the memory layout must change, or a paid account is needed for extended address space | Self-test in the shell: create and map exactly as ReXGlue does, report success and address ranges |
| Q2 | How much **RAM** may the app use before iOS ends it? | The game plus runtime must fit; one comparable port reports ~3 GB under a free ID | Already measured by the shell's "App memory available" line. **This needs the first device test of the existing shell** |
| Q3 | Do `getcontext`/`makecontext`/`swapcontext` work? | Decides T4 | Self-test: create a fiber, switch both ways, check that values survive |
| Q4 | Do `SIGSEGV`/`SIGBUS` handlers on a protected `shm` page work, and can the handler resume? | Needed for MMIO and GPU write-watching | Self-test: protect a page, write to it, catch the fault, unprotect, continue |

All four run in a few seconds and need **no game files**.

## 5. Implementation plan

Everything builds in GitHub Actions on macOS runners. **Nothing is installed on the Windows
PC.** No game files or generated code are involved anywhere in N1-b.

| Step | Work | Evidence that closes it |
|---|---|---|
| **B0 — device baseline** | You install the existing test shell on the iPad (no new code) | Screenshot plus `shell-log.txt`: Metal device, frame rate, controller input, **app memory limit** (answers Q2) |
| **B1 — iOS configure probe** | New CI job: configure the pinned ReXGlue for iOS (`CMAKE_SYSTEM_NAME=iOS`, `iphoneos`, arm64) **without patches**, and record every error | A list of real configure and build errors, replacing the predictions in §3 |
| **B2 — runtime self-test in the shell** | Add a "Runtime self-test" screen to the shell that performs Q1, Q3 and Q4 with plain system calls, copying ReXGlue's exact sizes and flags. This is still not ReXGlue itself, so it can run before the port exists | Device log answering Q1, Q3, Q4 |
| **B3 — patch set** | A `patches/rexglue-ios/` series in our repo, applied in CI to the pinned commit, implementing T1, T2, T5, T6, T7, T9, T10 | `rexbase`, `rexkernel` and the other modules compile for `ios-arm64` |
| **B4 — dependencies** | FFmpeg for iOS, MoltenVK static, SDL3 for iOS, Tracy off (T11) | Full runtime builds for iOS |
| **B5 — packaging** | Modules as embedded frameworks in the shell's bundle (T8 option A) | The IPA launches and loads every module (device log) |
| **B6 — simulator tests** | Build ReXGlue's unit tests for the iOS Simulator and run them on the macOS runner | Test counts, compared with macOS's 2,219 |
| **B7 — runtime start on device** | The shell initializes ReXGlue's runtime on the iPad: guest memory, kernel state, a Vulkan device through MoltenVK, an SDL3 window and controller input — **with no game loaded** | Device log: runtime up, Vulkan device name, memory ranges |

N1-b is complete when B7 passes on your iPad.

**Estimated effort:** two to four weeks of CI iteration, plus a device test after B0, B2, B5
and B7. This is a judgement; the uncertainty lies mostly in Q1 and in the third-party builds.

## 6. Decisions taken in this plan (reversible)

1. **Stay on current upstream ReXGlue (`ebac1cf`), BSD-3-Clause,** and write our own iOS
   patches, instead of moving to the older SDK the GPL ports use. Their patches would not
   apply to the current code. Our patches can stay permissive and be offered upstream.
2. **Patches live in this repository** as a numbered series applied in CI, not in a long-lived
   fork. This keeps the diff small and easy to rebase on newer upstream versions.
3. **Vulkan on MoltenVK first**; native Metal remains a later milestone (`ARCHITECTURE.md`
   R4). This is not a change of the final architecture; Metal is still the target.

## 7. Risks beyond N1-b (stated so they aren't forgotten)

- **GPU features:** Metal has no geometry shaders, and MoltenVK lacks some Vulkan features the
  Xbox 360 GPU translation can use. The MW 2005 port had to convert triangle fans to lists.
  This matters when a game draws, not in N1-b.
- **This game's memexport and readback paths** (`XBOX360_RECOMP_ASSESSMENT.md` §2) remain the
  largest correctness and performance risk on Apple GPUs.
- **Building the game's code for iOS** needs either macOS (cloud) or xtool. Generated code
  must not go to public CI. The decision is still open, and is needed before N2 (`ROADMAP.md`).
- **Codegen on the Windows PC** needs a local toolchain eventually, which waits on free space
  on drive C:. ReXGlue also publishes prebuilt SDK releases, which may avoid building the SDK
  locally (to check in N1-c).

## 8. Sources

- ReXGlue SDK at `ebac1cf0d04bab0042a5dd6148b34ab3953e9c0d`: `include/rex/platform.h`,
  `CMakeLists.txt`, `src/base/CMakeLists.txt`, `src/base/memory_posix.cpp`,
  `src/base/fiber_posix.cpp`, `src/base/relaunch_posix.cpp`,
  `src/base/instance_lock_posix.cpp`, `src/base/platform/file_posix.cpp`,
  `src/base/threading_posix.cpp`, `src/base/exception_handler_posix.cpp`,
  `src/base/module_group.cpp`, `include/rex/module.h`, `src/kernel/memory.cpp`,
  `src/gpu/xenos/host/library_name.h`, `src/gpu/xenos/host/vulkan/*`
- NFSMW-Recompiled-Mobile: `sdk/README.md`, `sdk/nfsmw-parches.diff` (file list only),
  `docs/parches.md`, `ios/README.md` (GPL-3.0; read, not copied)
- Theft4: `LIBERTYRECOMP_IOS_ARCHITECTURE.md` (GPL-3.0; read, not copied)
- CI evidence: Actions runs 44043947553 and 44069785573

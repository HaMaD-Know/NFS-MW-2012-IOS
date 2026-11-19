# Public Status

Last updated: 2026-11-19.

| Item | State | Evidence |
|---|---|---|
| Game port | **Not started.** No game code exists in this project | — |
| iPadOS test shell builds as an unsigned arm64 IPA in CI | **Yes** | Actions run 44043947553 |
| Test shell runs on an M2 iPad | **Yes** — iPad Pro 11" M2 (`iPad14,3`), iPadOS 26.6.1, sideloaded from run 44112115204 with a free Apple ID: Metal on "Apple M2 GPU", 2388 × 1668 drawable, runtime shader compile 214.6 ms, Xbox Wireless Controller connected, **5.00 GB** app memory available at launch | Device log 2026-11-17 |
| ReXGlue SDK builds from source on Windows (x64), Debug, with tests | **Yes**: 2,222 unit tests, 0 failed, 3 skipped | Actions run 44069785573 |
| ReXGlue SDK builds from source on macOS (Apple silicon), Debug, with tests | **Yes**, with retries: 2,219 pass, 2 skipped. Timing-sensitive tests may need a second attempt on the shared runner (this run: `folder_budget prunes older runs`, `lost device keeps crediting`) | Actions run 44069785573 |
| ReXGlue SDK builds on the developer's 8 GB Windows PC | Not attempted | — |
| ReXGlue runtime **compiles and links** for iOS arm64 (iPhoneOS SDK, Debug) | **Yes**, with the three patches in `patches/rexglue-ios/`: 925/925 build steps, 0 errors; runtime modules linked as iOS libraries | Actions run 44116653662 |
| ReXGlue configures for iOS without patches | No — one CMake error (recompiler CLI install rule) | Actions run 44113373357 |
| iPad shell with runtime self-test (address space, guest memory views, fault handling, fibers) builds | **Yes** | Actions run 44112115204 |
| Runtime self-test on the M2 iPad: address space | **PASS** — largest single reservation 5.00 GB, total reservable 6 GB; a 4.5 GB guest reservation fits, leaving about 1.5 GB for everything else | Device log 2026-11-17, 8 identical runs |
| Runtime self-test: guest memory via `shm_open` (ReXGlue's method) | **FAIL** — `shm_open`: Operation not permitted (errno 1). POSIX shared memory is blocked for this app. **Blocker**; an alternative backing is needed | same |
| Runtime self-test: memory-fault handling | **Not determined** — the test's setup also used `shm_open` and failed before the fault test ran | same |
| Runtime self-test: ucontext fibers (ReXGlue's method) | **FAIL** — `getcontext`: Operation not supported (errno 45). **Blocker**; needs a custom arm64 context switch | same |
| Replacement guest memory on the iPad: A temporary file / B Mach memory entry / C `vm_remap` | **All three PASS on the M2 iPad** (iPadOS 26.6.1, two identical runs): ReXGlue's 4.5 GB nine-view layout, all mirrors consistent, fault handling with resume on a mirrored page (SIGBUS). 256 MB write: A ~5.4–6.2 GB/s, not counted against app memory; B ~15.0–15.2 GB/s and C ~15.3–15.5 GB/s, counted (4.93 → 4.68 GB) | Shell 0.0.2 (run 44135637100), device log 2026-11-18 |
| Memory-fault handling on the iPad | **PASS** (write and read faults caught, page unprotected, access resumed) | same |
| arm64 fiber switch on the iPad (replacement for ucontext) | **PASS**: round trips, fiber-to-fiber, ~200 KB stack use, 6.5–6.7 ns per switch | same |
| Guest memory inside ReXGlue on the iPad (patch 0004 in real use) | **PASS (shell 0.1.2)**: `TitleRuntime::Load` in tool mode (no executable, no game) mapped ReXGlue's guest memory (virtual base 0x300000000, physical base 0x400000000) and created the kernel services; 1 MiB of title memory and 64 KiB of physical memory allocated, written and read back, and the physical page seen through its 0xC0000000 mirror view; clean unload and shutdown. Fibers: see the next row | Device log 2026-11-19 |
| ReXGlue's own fibers on the iPad (patch 0005 in real use) | **PASS (shell 0.1.3)**: `rex::platform::Fiber` on a converted thread, 100,000 rounds of three switches (count exact), a floating-point value kept across switches, a ~200 KB call stack in a fiber, clean destruction; 25.4 ns per switch in the Debug build | Device log 2026-11-19 |
| Audio through ReXGlue on the iPad | **PASS (shell 0.1.3)**: `apu.driver=sdl3` on iPadOS, 6-channel output, a 2-second 440 Hz tone submitted as 374 frames, all accepted and heard; 41 ms latency; 18 underruns (the test's 1 ms polling producer, not a title's audio path) | Device log 2026-11-19 |
| Importer checks on the iPad (synthetic data only) | **PASS (shell 0.1.3)**: 13 of 13 cases on folders and Xbox 360 disc images read in place (complete, missing, damaged, different game, other edition, no manifest), plus a non-image rejected. No real game data is used for the project | Device log 2026-11-19 |
| "Run all tests" on the iPad | **17 of 17 passed (shell 0.1.3)**, one launch: libraries, address space, three memory methods, standalone fibers, the two iPadOS-blocked originals failing as expected, Vulkan, importer, and one ReXGlue run with window, graphics, controller, audio, guest memory, fibers and clean shutdown | Device log 2026-11-19 |
| ReXGlue compiles for iOS with patches 0001–0005 (guest memory via a **Mach memory entry**, arm64 fibers) | **Yes**: 0 errors (earlier tmp-file variant: run 44135637165) | Run 44284348684 |
| iPad app with the ReXGlue runtime embedded (14 ReXGlue libraries, SDL3, MoltenVK as `libMoltenVK.dylib`, patches 0001–0006) builds in CI | **Yes**: all libraries arm64 + iOS, every `@rpath` dependency present, app 57 MB | Run 44289701605 |
| Those libraries load on the iPad | **Yes**: 16 of 16 ReXGlue libraries load inside the app, 14 runtime modules answer (shells 0.0.3–0.0.6) | Device logs 2026-11-18 |
| Vulkan via MoltenVK on the iPad | **PASS (shell 0.0.6, two runs, no crash)**: MoltenVK 1.4.2, `vkCreateInstance` succeeds, device "Apple M2 GPU", Vulkan 1.2, Metal 4 / Apple GPU family 8, 5,461 MB GPU memory available. Fixed by patch 0007 (host allocator passes through to malloc/free on iOS) | Device log 2026-11-18 |
| ReXGlue runtime starts on the iPad, no game (step B7, part 1) | **PASS (shell 0.0.7)**: ReXGlue's own startup ran headless (no window, audio, input or graphics driver): settings, log file, crash reporter, all eight system modules (filesystem, input, audio, UI, graphics, XAM, XBDM, kernel) loaded from the app, connected and initialized; then a clean shutdown (exit code 0). A second run in the same launch is refused by design | Device log 2026-11-18 |
| ReXGlue with graphics, window and controller on the iPad, no game (step B7, part 2) | **PASS (shell 0.0.9, no crash)**: ReXGlue's Vulkan graphics driver on the Apple M2 GPU through MoltenVK (Vulkan device created, 2388 × 1668 swapchain in an SDL3 uikit window), the frame loop at about 60–100 passes a second for 15 s, and SDL3 controller input: an Xbox Wireless Controller (045e:02e0) read through ReXGlue's input system with buttons, both triggers (0–255) and both sticks (full range) changing as pressed. Clean shutdown. Needs patch 0008 (SDL3 uikit video driver and Metal layer on iOS). Shell 0.0.8 had crashed after the test in CFPreferences; fixed in 0.0.9 | Device log 2026-11-19 |
| ReXGlue runtime **runs** on the iPad | **Not attempted.** Compiling for iOS does not show that it runs | — |
| Analysis of an Xbox 360 executable | Not attempted; requires the user's own copy | — |
| Bring Your Own Game: import, verification, re-selection | **Designed, not implemented** (`BYOG_DESIGN.md`) | — |
| Supported game edition and manifest | Not determined (`SUPPORTED_GAME_DATA.md`) | — |
| Importing a real game on an iPad | **Not done.** Will not be claimed until tested on a real iPad | — |

**Test environment note (2026-11-18):** all iPad results above were measured with the shell running
**inside LiveContainer** (free Apple ID), not installed directly. LiveContainer runs apps inside its own
process with its own entitlements, so the memory limit and process behaviour may differ for a directly
installed app.

| Vulkan via MoltenVK on the iPad, history | **Crashed in shells 0.0.3–0.0.5, fixed in 0.0.6** in `vkCreateInstance`: MoltenVK freed a libc++-allocated block through ReXGlue's operator delete and ReXGlue's host allocator aborted. Cause: inside LiveContainer the app is not the process's main executable, so ReXGlue's process-wide new/delete replacement does not cover the system C++ library. Fix in test: patch 0007 (host allocator passes blocks straight to malloc/free on iOS) | Device logs 2026-11-18 |

Nothing in this table is marked as working until there is evidence for it.

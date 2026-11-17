# Public Status

Last updated: 2026-11-17.

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
| Replacement guest memory (A temporary file / B Mach memory entry / C `vm_remap`) and arm64 fibers | **Pass on the macOS CI runner only** (logic check). **Not yet tested on the iPad**; no claim that they work on iPadOS | Run 44135637100 |
| ReXGlue compiles for iOS with patches 0001–0005 (including the tmp-file memory backing and arm64 fibers) | **Yes**: 925/925 steps, 0 errors | Run 44135637165 |
| ReXGlue runtime **runs** on the iPad | **Not attempted.** Compiling for iOS does not show that it runs | — |
| Analysis of an Xbox 360 executable | Not attempted; requires the user's own copy | — |
| Bring Your Own Game: import, verification, re-selection | **Designed, not implemented** (`BYOG_DESIGN.md`) | — |
| Supported game edition and manifest | Not determined (`SUPPORTED_GAME_DATA.md`) | — |
| Importing a real game on an iPad | **Not done.** Will not be claimed until tested on a real iPad | — |

Nothing in this table is marked as working until there is evidence for it.

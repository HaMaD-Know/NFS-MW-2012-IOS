# Public Status

Last updated: 2026-11-17.

| Item | State | Evidence |
|---|---|---|
| Game port | **Not started.** No game code exists in this project | — |
| iPadOS test shell builds as an unsigned arm64 IPA in CI | **Yes** | Actions run 44043947553 |
| Test shell runs on an M2 iPad | Pending a device test | Screenshot and `shell-log.txt` |
| ReXGlue SDK builds from source on Windows (x64), Debug, with tests | **Yes**: 2,222 unit tests, 0 failed, 3 skipped | Actions run 44069785573 |
| ReXGlue SDK builds from source on macOS (Apple silicon), Debug, with tests | **Yes**, with retries: 2,219 pass, 2 skipped. Timing-sensitive tests may need a second attempt on the shared runner (this run: `folder_budget prunes older runs`, `lost device keeps crediting`) | Actions run 44069785573 |
| ReXGlue SDK builds on the developer's 8 GB Windows PC | Not attempted | — |
| ReXGlue runtime **compiles and links** for iOS arm64 (iPhoneOS SDK, Debug) | **Yes**, with the three patches in `patches/rexglue-ios/`: 925/925 build steps, 0 errors; runtime modules linked as iOS libraries | Actions run 44116653662 |
| ReXGlue configures for iOS without patches | No — one CMake error (recompiler CLI install rule) | Actions run 44113373357 |
| iPad shell with runtime self-test (address space, guest memory views, fault handling, fibers) builds | **Yes** | Actions run 44112115204 |
| Runtime self-test results on the M2 iPad | **Not run yet** — needs the device | — |
| ReXGlue runtime **runs** on the iPad | **Not attempted.** Compiling for iOS does not show that it runs | — |
| Analysis of an Xbox 360 executable | Not attempted; requires the user's own copy | — |
| Bring Your Own Game: import, verification, re-selection | **Designed, not implemented** (`BYOG_DESIGN.md`) | — |
| Supported game edition and manifest | Not determined (`SUPPORTED_GAME_DATA.md`) | — |
| Importing a real game on an iPad | **Not done.** Will not be claimed until tested on a real iPad | — |

Nothing in this table is marked as working until there is evidence for it.

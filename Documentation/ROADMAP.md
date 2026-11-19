# Roadmap

Revision 3, 2026-11-17, after the native-port decision.

**Goal:** a true native ARM64 iPadOS port of Need for Speed: Most Wanted (2012) — original
cars, world, sounds, physics and gameplay; Apple Metal for graphics; controllers only in
version 1.

**Excluded as the final solution (owner's decision):** BCD, Madeira, FEX, Wine, DXMT and any
Windows compatibility layer.

> Some research notes referenced here (`GAME_ANALYSIS.md`, `FEASIBILITY.md`, `COMPATIBILITY_RESEARCH.md`,
> `NATIVE_PORT_RESEARCH.md`, `XBOX360_RECOMP_ASSESSMENT.md`, `DEVELOPMENT_STATUS.md`) are kept privately and are not
> in the public repository. `PUBLIC_STATUS.md` is the public record of results.

**Standing rules**

- The original game folder is never modified, executed or used as a recompilation input.
- No DRM or licence check is removed or bypassed.
- Only the owner's own, legitimately obtained copies are used as inputs.
- No game code, generated code or assets are committed, bundled or redistributed.
- **Bring Your Own Game (binding, 2026-11-17):** the public IPA contains no game files,
  assets or game-derived code. Users import their own Xbox 360 data through the iPadOS Files
  picker, and their game code is a personal module built from their own executable
  (`BYOG_DESIGN.md`). User files are never uploaded anywhere.
- Game import is not described as working until it is implemented and tested on a real iPad.
- No compatibility or performance claim without a recorded test.
- Each milestone ends at a gate that needs the owner's approval.

## Completed

| Milestone | Result | Document |
|---|---|---|
| 0 — Technical investigation | PC executable and data analysed | `GAME_ANALYSIS.md` |
| 0.5 — Compatibility research | BCD and Madeira studied; approach later rejected by the owner | `COMPATIBILITY_RESEARCH.md` |
| 0.7 — Native port research | Console static recompilation identified as the realistic route | `NATIVE_PORT_RESEARCH.md` |
| 0.8 — Xbox 360 assessment | Game reaches gameplay in Xenia; licences verified; Mac-free build routes identified | `XBOX360_RECOMP_ASSESSMENT.md` |

**Waiting for review.**

## Milestone N1-b — ReXGlue runtime on iPadOS arm64 (no game files) — exit condition met 2026-11-19

*Renumbered 2026-11-17 to match the owner's plan: N1-b is now the iOS runtime port, and the
owner decisions previously called N1-b move into the N1-c gate.*

Plan, tasks and device questions: `N1B_IOS_FEASIBILITY.md`. Steps B0–B7:
- B0: device baseline with the existing shell.
- B1: unpatched iOS configure probe in CI.
- B2: runtime self-test screen (address space, fibers, fault handling).
- B3–B5: patch series, dependencies and packaging.
- B6: unit tests in the iOS Simulator.
- B7: runtime start on the iPad with no game loaded.

Exit: B7 passes on the owner's M2 iPad.

**Status 2026-11-19: exit condition met.** B7 passed on the owner's M2 iPad (shells 0.0.7–0.0.9): ReXGlue starts, runs its Vulkan graphics driver through MoltenVK in its own window, reads an Xbox controller, and shuts down cleanly, with no game. Still open and carried forward: guest memory and fibers inside ReXGlue (they start only with a title), audio, and B6 (simulator tests).

## Milestone G1 — Import Your Game, without game files (can run alongside N1-b)

Build the BYOG importer in the existing shell and test it with **synthetic folders** that
mimic the structure but contain no game content.

| Step | Done when |
|---|---|
| First-launch "Import Your Game" screen; *Choose Game Folder* / *Choose Disc Image* through `UIDocumentPickerViewController` | Picker opens on the iPad and returns a folder or file |
| Security-scoped access and bookmarks; "use in place" and "copy into the app" (free-space check, atomic copy, excluded from backup) | Both modes work with a synthetic folder on internal storage and on a USB-C drive |
| Verification engine (structure → identity → completeness) driven by a manifest; a **synthetic test manifest** | Every state in `BYOG_DESIGN.md` §6 is shown correctly, including each error message |
| Re-selection: stale bookmark, moved folder, replaced data | *Needs attention* → *Choose Again* recovers; saves untouched |
| Game Data settings page | Shows status, location, size; Verify Again, Choose Again, Remove |
| Unit tests for the verifier in CI (iOS Simulator) | Green in CI |

Exit: all of the above recorded on the M2 iPad (screenshots + log). This proves the
importer, **not** that the real game imports.

## Milestone G2 — Real manifest and verification (after N1-c, owner's copy)

- Confirm title ID, media ID, executable hash and title-update needs from the owner's own
  copy, and fill in `SUPPORTED_GAME_DATA.md`.
- Generate the edition's manifest (paths, sizes, SHA-256) **locally**; commit only the
  manifest.
- Import the owner's real copy on the iPad in both storage modes and verify it.

Exit: the owner's copy shows "Game data verified" on the iPad; deliberately removed and
altered files produce the right messages.

## Milestone G3 — Personal game module

Depends on the R7 build-route decision and on N2 (the recompiled game running on the PC).

- Package the recompiled code as `NFSMW2012Game.framework` with a stable ABI to the public
  app and an embedded executable hash.
- Loader in the app: detect the module, check the ABI version and the executable binding,
  and show "Game module missing / mismatched" otherwise.
- Document the private build of the module for a user, and the way to add it when signing
  (sideloading tool's framework injection) or to build a personal IPA.

Exit: on the iPad, the public IPA plus the owner's own module plus the owner's imported data
reaches "Ready", and the game starts.

## Milestone N1-a — Toolchain proof ✅ 2026-11-17

Completed in CI. The ReXGlue SDK builds with its tests on Windows x64 and macOS arm64
(Actions run 44069785573), and the iPad test shell builds as an unsigned arm64 IPA (run
44043947553). Still open from N1-a: the device test of the shell (now B0) and the local
toolchain install (postponed until drive C: has space).

Original N1-a plan, kept for reference:

Can start immediately and needs no game file and no decision about decryption.

| Step | Done when |
|---|---|
| 1. Install Visual Studio 2026 (C++ and Clang), CMake, Ninja, Vulkan SDK on the PC | Versions recorded |
| 2. Build the ReXGlue SDK with the `win-amd64` preset; record time, peak memory and disk use on 8 GB | `rexglue --help` works; numbers recorded |
| 3. Run the SDK's own test suite | Results recorded |
| 4. Create the public repository skeleton: licence choice, `.gitignore` that blocks game and generated files, README stating the "bring your own disc" rule | Repository exists |
| 5. Public GitHub Actions: build the SDK on Windows and macOS, and an **empty iPadOS app shell** (Swift, Metal clear-screen, GameController detection) packaged as an unsigned IPA | CI green; IPA artifact produced |
| 6. Owner sideloads the empty shell on the M2 iPad from Windows | It launches and detects the controller |
| 7. Read Apple's current Xcode and SDK licence; record whether the xtool route is acceptable | Decision recorded |

Exit: the toolchain builds on this PC and CI produces an installable empty iPad app.

The test shell is not the port and must never be described as one.

## Milestone N1-b — Owner decisions

See the Step A table below: console copy, backup, decryption, console assets, personal or
public, iOS build route.

## Milestone N1-c — Analysis of the owner's executable (no port code)

Only after N1-b. It is Steps B–D below, with the analysis run by the owner on their own copy.

## Milestone N1 (detail) — Feasibility probe (no port code)

Purpose: turn "possible in principle" into numbers for this game, before any commitment.

### Step A — Owner decisions

| Decision | Why it matters |
|---|---|
| Is an Xbox 360 copy of the 2012 game available (disc or legitimate digital purchase)? Is a Wii U copy? | The source platform decides the toolchain |
| How will a personal backup be made? | Needs specific hardware; nothing starts without it |
| Is decrypting one's own console disc image acceptable? | Every console recompilation depends on it; legality varies by country (`NATIVE_PORT_RESEARCH.md` §7) |
| Is console-level texture quality acceptable for version 1? | The Xbox 360 route cannot deliver PC textures |
| Personal build only, or an intended public release? | A public release cannot ship ready-to-run recompiled code |

If no lawful console source is available, the native route stops here.

### Step B — Close the PC question (one read-only check)

If a licensed PC copy exists, run `Tools/pe_inspect.py` on its `NFS13.exe` and record section
names, entry-point section and entropy. This confirms or refutes that the licensed
executable is DRM-wrapped. If it is wrapped, the PC route is closed for good.

### Step C — Tooling review

Done in Milestone 0.8. Licences are confirmed. **ReXGlue is chosen over bare XenonRecomp**,
because it includes the runtime (kernel, memory, audio, input, GPU) that XenonRecomp does not
provide, and it has a Windows build. The setup guide becomes part of N1-a.

### Step D — Analysis of the owner's console executable (analysis stage only)

4. Run the SDK's analysis on the owner's own executable. Record: code size, function count,
   functions without boundary data, detected and undetected jump tables.
5. List every kernel and system import and mark which the SDK implements.
5a. Check whether ReXGlue contains the equivalent of Xenia Canary's October 2026 memexport
    write-back fix (commit `1d8489b`) and the depth-clamp and readback settings this game
    needs.
6. Identify the audio, video and save-data mechanisms the game uses.
7. Compare the console data bundles with the PC ones using `Tools/bnd2_survey.py`
   (platform field, resource types, sizes) to quantify the asset difference.

No recompiled code is compiled or run in this milestone.

### Deliverable and gate

`PROBE_REPORT.md` with the numbers above, a list of the hardest expected problems, a revised
effort estimate and a go / no-go recommendation.

Gate N1: owner approves continuing, switches to the Wii U candidate, or stops.

## Milestone N2 — Boot on the Windows PC

Recompile, build for Windows, fix until the game reaches its menus. Iterate on the PC, where
turnaround is minutes instead of a device install. Exit: menus with sound, controller input.

## Milestone N3 — Gameplay on the Windows PC

Free roam, a race and a pursuit complete; saves work; online features stubbed for offline
play. Exit: recorded test route without crashes.

## Milestone N4 — First run on the M2 iPad

ARM64 build, Swift shell, game-folder selection, GameController input, Vulkan-on-Metal
graphics, log export. Build from Windows with `xtool` or on cloud macOS. Exit: menus and
free roam on the device.

## Milestone N5 — Correctness and performance on the iPad

Rendering compared against the console original; audio and video; memory, thermals, frame
pacing. Resolution scaling and filtering as explicit settings. Exit: stated frame-rate
target on the test route, or the shortfall documented.

## Milestone N6 — Direct Metal renderer

Replace the translated graphics path with Metal draw paths for this game, verified frame by
frame against stage 1. Exit: visual parity and a measured performance gain.

## Milestone N7 — Version 1

Controllers only. Settings, save management, and the BYOG flow (G1–G3):
- Import Your Game, verification status, re-selection.
- A personal game module built from the user's own executable.

Public distribution means the public IPA (no game content) plus documentation. It never
includes game code, generated code or assets.

## Optional side track — PC data tools

Finish the bundle reader (85 unparsed map bundles; texture and mesh decoding). Supports the
asset comparison in N1 and any later research into using higher-resolution textures.

## Parked or closed

| Item | State |
|---|---|
| Windows compatibility stack (BCD, Madeira) | Rejected by the owner as the final solution; research kept |
| Static recompilation of the PC executable | Closed unless Step B shows an unencrypted licensed executable; even then second choice |
| Decompilation to source | Not achievable without symbols |
| Engine reimplementation | Viewer-level only; not the port |
| Wii U recompilation | Fallback candidate, examined only if Gate N1 points to it |

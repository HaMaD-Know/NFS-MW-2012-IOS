# Architecture

> Some research notes referenced in this document (`GAME_ANALYSIS.md`, `FEASIBILITY.md`, `COMPATIBILITY_RESEARCH.md`,
> `NATIVE_PORT_RESEARCH.md`, `XBOX360_RECOMP_ASSESSMENT.md`, `DEVELOPMENT_STATUS.md`) are kept privately and are not
> in the public repository. `PUBLIC_STATUS.md` is the public record of results.


Current status: **Revision 3 (native port) is in force — see the next section.** Revision 2
(compatibility stack, approach B) was adopted and then rejected by the owner on 2026-11-17;
its text remains further down as a record. Nothing here is built.

---

# Revision 3 — Native port architecture (2026-11-17)

**This section supersedes everything below it.** *(Amended 2026-11-17 by R8, Bring Your Own
Game: game data is imported by the user at run time; game code is a personal module and is
never part of the public IPA.)* The owner has decided on a true native
ARM64 port and excluded BCD, Madeira, FEX, Wine, DXMT and any Windows compatibility layer as
the final solution. Sections 1–6 describe the rejected compatibility approach and are kept
only as a record.

Status: **proposal from desk research. Nothing is built or tested.** Source and reasoning:
`NATIVE_PORT_RESEARCH.md`.

## R1. Chosen direction

Static, ahead-of-time recompilation of a **console build** of the game — the Xbox 360
version unless the probe in `ROADMAP.md` says otherwise — into C++, compiled to ARM64, with
a purpose-built runtime. No JIT, no emulator, no Windows API.

```
Owner's Xbox 360 copy (personal backup)
   │
   │  OFFLINE, on the development PC
   ▼
default.xex ──► analysis (functions, jump tables, imports)
            ──► PowerPC → C++ recompiler            [ReXGlue SDK / XenonRecomp]
            ──► generated C++  (never committed, never distributed)
   │
   ▼  compiled with clang for ARM64
┌───────────────────────────────────────────────────────────────────────┐
│ iPad app — native ARM64, no JIT                                       │
│                                                                       │
│  Recompiled game code      physics, AI, traffic, police, races,       │
│  (translated PowerPC)      streaming, UI logic — the original code    │
│                                                                       │
│  Runtime                   Xbox 360 kernel and system functions,      │
│  (hand-written / SDK)      file system, threads, memory, saves        │
│                                                                       │
│  Graphics                  Stage 1: console GPU commands → Vulkan →   │
│                                     MoltenVK → Metal                  │
│                            Stage 2: direct Metal renderer             │
│                                                                       │
│  Audio                     console audio decode → AVAudioEngine /     │
│                            CoreAudio                                  │
│  Input                     GameController framework (controllers      │
│                            only in version 1)                         │
│  App shell                 Swift: lifecycle, game-folder selection,   │
│                            settings, log export                       │
│                                                                       │
│  Game data                 original console data files, user-supplied │
└───────────────────────────────────────────────────────────────────────┘
```

## R2. Why this and not the PC executable

| | PC executable | Xbox 360 executable |
|---|---|---|
| Licence protection | Very likely wrapped by EA's account-activation DRM; a recompiler cannot read it without removing that, which is excluded | Platform-level encryption of the owner's own disc; no account licence check. Decrypting it is still a legal question for the owner (`NATIVE_PORT_RESEARCH.md` §7) |
| Recompiler maturity | One demonstrated port of a much smaller D3D9 game | Several ports in 2026, including iPad builds |
| Platform layer needed | New Win32 layer and a new Direct3D 11-on-Metal layer | Existing open runtimes derived from Xenia |
| Assets | PC quality | Console quality |
| Input | Keyboard, mouse, pads | Controllers only — matches version 1 |

## R3. Honest labels

- The app's code is native ARM64 and is produced ahead of time. It is not source-level: the
  game logic is machine-translated and still addresses a simulated console memory space.
- A runtime that stands in for the console is unavoidable in this approach. It is linked
  into the app; it is not a general emulator and executes no guest instructions at run time.
- Gameplay, physics and progression are the original code and therefore preserved exactly.
  Assets are those of the console version.
- Moving from "Vulkan on Metal" to "direct Metal" is a later stage, as it was for Theft4.

## R4. Stages toward more native code

1. Recompiled game + SDK runtime, running on the Windows PC (fast iteration, no device).
2. Same code on the iPad through Vulkan-on-Metal.
3. Direct Metal renderer for this game's draw paths.
4. Native replacements for specific hot or problematic subsystems, one at a time, each
   verified against the recompiled original.

## R5. Decision gates

```
Gate N0  Owner decisions: console copy available? decryption of own disc acceptable?
   │        No ─────────────────────────► No lawful native route. Stop or reconsider.
   ▼
Gate N1  Probe report (analysis only, no port code): function count, jump tables,
   │     unimplemented imports, executable size, SDK fit.
   │        Not tractable ──────────────► Evaluate the Wii U candidate, or stop.
   ▼
Gate N2  Boots to menus on the Windows PC.
   ▼
Gate N3  Free roam and one race on the Windows PC.
   ▼
Gate N4  First run on the M2 iPad.
```

## R6. Repository structure for this direction

Nothing is created until Gate N1 passes. Expected shape, to be aligned with the chosen SDK's
project template rather than invented here:

```
├── App/                 Swift shell (iPadOS)
├── Runtime/             game-specific runtime code and SDK patches
├── Recomp/              recompiler configuration (function and jump-table lists)
│   └── generated/       generated C++ — git-ignored, never published
├── Renderer/            Metal renderer (stage 3)
├── Tools/               analysis and diagnostics
├── Tests/
├── Documentation/
├── third_party/         SDK as a submodule
└── .github/workflows/   builds producing an unsigned IPA
```

## R7. Build and signing

Revised 2026-11-17 from `XBOX360_RECOMP_ASSESSMENT.md` §5.

| Build | Where | Contains game-derived code? |
|---|---|---|
| ReXGlue SDK, app shell, tests | **Public GitHub Actions** (Windows and macOS runners, free on public repositories) | No |
| Codegen and the Windows build of the game | **Local Windows PC only** (`win-amd64` preset, Clang, Vulkan SDK) | Yes — never leaves the PC |
| iPad build of the game | Owner's choice: xtool under WSL on the PC (works; Apple licence question), a private macOS CI run, or a rented Mac | Yes |

- Upstream ReXGlue has no iOS preset and no Metal driver. The iOS platform layer is new work,
  or is borrowed from Theft4 or NFSMW-Mobile, which makes the project GPL-3.0.
- Metal shaders: compiled offline with Apple's compiler where a Mac is available, otherwise
  compiled from source at run time on the device.
- Unsigned IPA; signing with the owner's Apple ID; no secrets in the repository.
- No JIT entitlement. Memory entitlements may need a paid account (a comparable port reports
  about 3 GB usable under a free Apple ID).
- Generated code and any game file are excluded from the repository, from CI logs and
  artifacts, and from releases.

## R8. Bring Your Own Game (adopted 2026-11-17, binding)

Full design: `BYOG_DESIGN.md`. Supported data: `SUPPORTED_GAME_DATA.md`. Nothing here is
implemented yet.

- **The public IPA contains no game files, no game assets and no code derived from the game.**
  It holds the Swift shell, the importer and verifier, the ReXGlue iOS runtime, the renderer,
  audio and input, and a manifest of the supported edition's file names, sizes and hashes.
- **Game data** (the user's own Xbox 360 copy) arrives at run time through the iPadOS
  document picker. It is either copied into `Application Support/GameData/` (excluded from
  backups) or used in place through a security-scoped bookmark. It is verified for structure,
  edition and completeness, can be re-selected, and is never uploaded.
- **Game code** cannot be imported at run time on iPadOS. Recompiled code must be compiled
  and signed into the app; JIT and loading unsigned code are forbidden, and an interpreter
  is excluded. It is therefore a **personal game module** (`NFSMW2012Game.framework`):
  - built privately from the user's own executable;
  - added to the IPA when the user signs it, or built into a personal IPA;
  - bound by SHA-256 to that exact executable.
- **Consequence for the user experience:** with the public IPA alone, the flow ends at "Game
  data verified — game module missing". Launching needs the personal module. The module's
  build route is the open R7 decision.
- Game data and saves are separate; replacing game data never touches saves.

The R1 diagram changes accordingly. The "generated C++" box is produced from the *user's* own
executable on the *user's* machine, and is linked into the personal module, not into the
public app.

---

# Earlier revisions (superseded)

## 1. What changed from the brief, and why

The brief proposed a Swift app on top of a custom C++ engine with hand-written
`Compatibility/WindowsAPI`, `GraphicsAPI`, `AudioAPI` and `Runtime` layers.

Milestone 0 found that the game is a closed, 32-bit x86, Direct3D 11 Windows program with
about 210 Win32 imports and no source (`GAME_ANALYSIS.md`). Making it run requires three very
large components — an x86→ARM64 translator, a Windows API implementation and a D3D11→Metal
translator. All three already exist as mature open-source projects and have already been
assembled for iOS (`FEASIBILITY.md` §4B). Rewriting them is not a credible plan.

**Decision:** the project integrates and tunes an existing stack. It does not implement an
engine or a Windows layer.

## 2. Recommended architecture (approach B)

```
┌──────────────────────────────────────────────────────────────────────┐
│ iPad app  (ARM64, sideloaded, JIT enabled by a debugger helper)      │
│                                                                      │
│  App layer — Swift / SwiftUI                    ← this project       │
│    game-folder import, per-game profile, touch layout, controller    │
│    mapping, settings, log capture and export                         │
│                                                                      │
│  Windows environment — Wine (ARM64EC, WoW64 for 32-bit)  ← upstream  │
│    kernel32/user32/advapi32/ole32/… , registry, file system,         │
│    dsound → CoreAudio, xinput/dinput → GameController                │
│                                                                      │
│  CPU — FEX-Emu  x86 → ARM64 JIT                          ← upstream  │
│  Graphics — DXMT  D3D11 + DXBC shaders → Metal           ← upstream  │
│                                                                      │
│  Guest — NFS13.exe and game data, unmodified, user-supplied          │
│          (plus the EA app, if a genuine copy needs it)               │
└──────────────────────────────────────────────────────────────────────┘
```

How the game's needs map onto the stack:

| Game requirement | Provided by | Project work |
|---|---|---|
| x86-32 code, fixed base `0x400000` | FEX + Wine WoW64 | None expected; verify |
| Win32, COM, WMI, registry, INI | Wine | Fix gaps only if logs show them |
| Direct3D 11, DXGI, `D3DReflect` | DXMT; `d3dcompiler_43` from Wine or the DirectX redistributable | Shader/feature fixes if rendering is wrong |
| DirectSound, waveOut | Wine audio driver | None expected |
| XInput, DirectInput 8 | Wine → GameController | Controller profile, touch layout |
| Keyboard/mouse UI | Runner's input layer | Touch-to-input mapping for menus |
| Origin SDK / EA app | EA's own client running in the same environment | **Unknown; test first** |
| Save games (Documents) | Wine prefix inside the app container | Export / backup |

## 2a. How the existing stack implements this (revision 2)

Verified against Madeira's and BCD's documents; identical design in both.

```
One iOS process (iOS apps cannot spawn programs)
│
├─ Native ARM64, statically linked into the app
│    Wine unix side: ntdll, wineserver (as a thread), win32u, iOS display/input driver
│    FEXCore (translator) and the dual-mapped RX/RW JIT code pool
│    DXMT unix side: Metal renderer + DXBC→Metal shader converter (embeds LLVM)
│    Audio mixer, GameController input, touch overlay, launcher UI (Swift)
│
├─ Wine "PE" DLLs
│    ARM64EC farm  → used by 64-bit games, runs natively
│    i386 farm     → used by 32-bit games (717 modules), translated by FEX
│    WoW64 layer (wow64.dll, wow64win.dll) → native ARM64
│
└─ Per 32-bit Windows process: a 4 GB "guest window" at host base B
     guest address a  ⇄  host address B + a
     NFS13.exe at guest 0x400000 (its required fixed base)
     DXMT's i386 d3d11.dll / dxgi.dll inside the guest, calling the native renderer
     through a 32-bit thunk table
```

Consequences for this game:

| Property | Effect |
|---|---|
| iOS reserves the low 4 GB of every process | Solved by the guest window; the game sees a normal 32-bit space |
| D3D11 front-end is i386 for 32-bit games | Graphics API calls are translated code; CPU cost per draw call is higher than for 64-bit games |
| Shaders converted and cached on first use | Expect first-run stutter; a warmed cache matters |
| A launcher that starts the game is a second "process" | Needs a second 4 GB guest window |
| Nothing "activates" a window on iOS | Games that require foreground/focus need the runner's focus fixes — the reported wall for this game |
| Per-app memory limit ~8.2 GB on an 8 GB M2 (with the increased-memory entitlement) | Guest ≤ 4 GB, plus Wine, JIT pool and Metal textures must fit |

## 2b. Foundation choice (revision 2)

| Role | Choice | Why |
|---|---|---|
| First test vehicle | **BCD** | Explicit support for iPadOS 26 on M2; tester logs from 8 GB M2 iPads; low-memory tiers; auto-tune and log export |
| Reference and fallback | **Madeira** | Upstream of BCD; documented internals; the only direct report of this executable running |
| Own application | **Not now** | See `FEASIBILITY.md` §8. Contribute a game profile and fixes upstream first |

The EA-client dependency is outside this architecture's control. Two routes exist
(`COMPATIBILITY_RESEARCH.md` §6); choosing one is an owner decision that precedes any test.

### What this project would actually own

1. **A game profile**: launch options, resolution and scaling policy, DXMT and FEX settings,
   memory settings — all as explicit, visible options, never silent downgrades.
2. **Controls**: a touch layout designed for this game (steer, throttle, brake, handbrake,
   nitrous, camera, EasyDrive) and a controller mapping.
3. **Diagnostics**: capture of Wine/FEX/DXMT logs and frame timing to files the user can
   export from the iPad without a Mac.
4. **Fixes**: targeted patches to the forked components where logs prove a defect,
   contributed back where the upstream accepts them.
5. **Read-only tools** for the game data (`Tools/`), used to understand rendering problems.

### Licensing consequence

Madeira and BCD are GPL-3.0-or-later; Wine is LGPL-2.1+; FEX and DXMT are MIT upstream with
GPL changes in the forks. An app derived from them must itself be GPL-3.0-or-later with
source published. No game file may ever be included in the repository or in a release.

### Honest labels

- The app is a native ARM64 iPadOS app. **The game inside it is not native**: its x86 code is
  translated at run time. Documentation and any release notes must say so.
- It depends on JIT obtained through a debugger helper and cannot be distributed through the
  App Store.

## 3. Decision gates

```
Gate 0  Owner decision: which licensed route?
   │      Route 1  Steam copy + EA's real client      (blocked upstream today)
   │      Route 2  EA-account copy + runner's EA store (experimental, unofficial client)
   │      Neither acceptable ───────► Stop approach B. Do not circumvent.
   ▼
Gate 1  Runner works on this iPad: JIT on, increased-memory entitlement present,
   │    a known-good free test program runs.
   │      No ───────────────────────► Blocked on device/OS. Record and stop.
   ▼
Gate 2  Milestone 0.6 test: licensed copy in the unmodified runner
   │
   ├─ runs to gameplay ───────────► Work = profile, controls, tuning, upstream contribution.
   │
   ├─ title screen, no input ─────► Known wall (Madeira #104). Test current builds of both
   │                                 runners; report upstream with logs; fix in Wine's iOS
   │                                 driver only if upstream cannot.
   │
   ├─ starts but fails/too slow ──► Diagnose from logs. Report upstream. Time-boxed.
   │
   └─ stops at EA licensing ──────► The chosen route does not work for this title.
                                     Stop. Do not circumvent.
```

Forking is not a gate outcome by default. It is considered only when a specific, proven fix
cannot go upstream.

## 4. Alternative kept on file: Xbox 360 static recompilation (A2)

Only if a personally owned Xbox 360 disc of the 2012 game is available and approach B fails.

```
360 XEX ──XenonRecomp──► C++ ──clang──► ARM64 code
                                          │
        runtime: Xbox 360 kernel shims, Xenos GPU → Vulkan/Metal, XMA audio, input
```

Produces a real native app with no JIT, but with console-quality assets, and requires
building or adapting a full console runtime. Multi-year effort; not planned.

## 5. Repository structure

The tree in the brief assumed a custom engine. Creating `Engine/Physics`, `Engine/Graphics`,
`Compatibility/WindowsAPI` and similar folders now would be empty scaffolding for code that
should not be written. The structure is therefore deferred until the gate above is passed.

Current, real contents:

```
NFS-MW-2012-iPad/
├── Documentation/                      the five Milestone 0 documents
├── Tools/
│   ├── pe_inspect.py                   read-only PE inspector
│   └── bnd2_survey.py                  read-only bundle survey
├── GameIntegration/ExecutableAnalysis/ machine-readable analysis output
├── GameData/README.md                  rules for user-supplied data (nothing else is committed)
└── .gitignore                          blocks game files from ever being committed
```

Likely structure if approach B proceeds (to be confirmed against the chosen upstream's own
layout rather than invented here):

```
├── App/            Swift app layer and resources
├── Profiles/       game profile, touch layouts, controller maps
├── Patches/        patch series against the pinned upstream forks
├── third_party/    submodules: runner fork (Wine, FEX, DXMT)
├── Tools/          analysis and diagnostics
├── Tests/
├── Documentation/
└── .github/workflows/ios-build.yml
```

## 6. Build and signing (for when there is something to build)

- Compile on GitHub Actions macOS runners; never on the 8 GB Windows PC.
- Produce an **unsigned IPA** as the workflow artifact. Sign it on the user's side with their
  own Apple ID through a sideloading tool. No certificate, key or provisioning profile is
  stored in the repository or printed in logs.
- Open risk: the upstream stack needs toolchains and inputs that are not in its repository
  and has long build times. Whether it builds within free CI limits is unverified; the first
  CI task is to reproduce the upstream build unchanged before modifying anything.

Facts established in revision 2 (`COMPATIBILITY_RESEARCH.md` §9):

| Fact | Implication |
|---|---|
| BCD's workflow is ~5,900 lines, `macos-15`, 360-minute timeout, restricted to its owner's repository; its builds currently run on the owner's Mac | A fork must remove the guard and will run at the hosted-runner time limit |
| Madeira's clean-checkout record says the clone alone does not build; inputs such as llvm-mingw, an iOS LLVM build and Apple's Metal Shader Converter are fetched separately | Reproducing the build is a project in itself |
| "Debug is the configuration that runs the games; Release builds have crashed the guest" | Use the upstream's configuration exactly |
| The app and its JIT helper extension must be signed together | The sideloading tool must keep app extensions |
| BCD publishes a complete source tarball with every release | The cleanest starting point if a fork is ever needed |
| Licence terms on BCD's own files | A fork needs its own name and icon and the BCD attribution line |

No build work is planned until a test result justifies it.

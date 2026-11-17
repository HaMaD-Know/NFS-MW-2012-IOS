# Bring Your Own Game (BYOG) — Design

Adopted 2026-11-17 as a **binding project requirement**. Status: **design only. Nothing in
this document is implemented or tested.** Game import must not be described as working until
it has been implemented and tested on a real iPad.

## 1. Requirements

| # | Requirement |
|---|---|
| R1 | The IPA and the public repository contain **no** *Need for Speed: Most Wanted* (2012) game files, assets or code derived from them |
| R2 | The user supplies their own legally obtained, compatible Xbox 360 game data |
| R3 | Import uses the native iPadOS document picker and security-scoped file access |
| R4 | The app checks the data for required files, completeness and compatibility, and shows the result |
| R5 | Missing files and incompatible versions get clear, specific messages |
| R6 | Game data lives in, or is accessed from, an app-managed location, and never leaves the device: no uploads to GitHub, analytics or any server |
| R7 | The user can select the game again if it was moved, replaced or deleted |
| R8 | Original assets are used as supplied; no silent re-encoding, resizing or quality reduction |
| R9 | No DRM bypass or authentication circumvention |
| R10 | The supported game version and data format are documented (`SUPPORTED_GAME_DATA.md`) |

## 2. The constraint that shapes BYOG on iPadOS

A static recompilation port has **two** game-dependent parts:

| Part | What it is | Can the user import it at run time? |
|---|---|---|
| **Game data** | The disc's files: bundles, textures, audio, video, and the executable `default.xex` as data | **Yes.** It is read through the Files picker like any document |
| **Game code** | The game's PowerPC code, recompiled ahead of time into native ARM64 C++ and compiled into machine code | **No, not at run time.** iPadOS runs only code that is compiled and signed into the app when it is installed. It forbids generating code (JIT) and loading unsigned code, and an interpreter would be the emulator this project excludes |

The recompiled code is also derived from EA's copyrighted executable, so under R1 it
**cannot** be in the public IPA either.

**Consequence:** the public IPA alone cannot run the game. The user's experience needs one
additional, one-time step beyond the flow in the requirements: producing their **personal
game module** from their own copy. The design below keeps that step separate, private and
explicit. It does not hide the step or pretend it isn't there.

This is how other recompilation projects handle it:
- The MW 2005 mobile port states that each person builds their own app from their own disc.
- Theft4 instead ships generated game code in a public IPA, which this project will not do.

## 3. Architecture

```
PUBLIC (repository and IPA — no game content)       PRIVATE (the user's own copy, on their devices)
┌────────────────────────────────────────┐          ┌──────────────────────────────────────────┐
│ NFS MW iPad app (public IPA)           │          │ User's game data (disc backup)           │
│  • Swift shell: importer, verifier,    │  Files   │  default.xex, bundles, audio, video, TU  │
│    settings, launcher, logs            │◄─picker──┤                                          │
│  • ReXGlue runtime (iOS port)          │          └──────────────────────────────────────────┘
│  • Renderer, audio, controller input   │                           │ (user's own machine)
│  • Game-module loader + ABI check      │                           ▼
│  • Manifest of the supported version   │          ┌──────────────────────────────────────────┐
│    (file names, sizes, hashes only)    │          │ Personal game module build (private)     │
└───────────────────┬────────────────────┘          │  codegen from the user's own default.xex │
                    │ loads at launch, if present   │  → NFSMW2012Game.framework (arm64)       │
                    ▼                               └───────────────────┬──────────────────────┘
        ┌──────────────────────────┐                                    │ added when the user signs
        │ NFSMW2012Game.framework  │◄───────────────────────────────────┘ the IPA (sideloading
        │ (the user's own, signed  │                                       tool's "inject framework"
        │ with their Apple ID)     │                                       option), or built into a
        └──────────────────────────┘                                       personal IPA
```

- **Public IPA:** the app, runtime and importer. It works on its own up to "Game data
  verified — game module missing".
- **Personal game module:** a framework the user builds from their own executable. It is
  never uploaded, never published, and never built on public CI.
- **Binding check:** a module is valid only for the exact executable it was built from. The
  module records that executable's SHA-256, and the app refuses to launch with a mismatch.

How the personal module is built is an **open decision** (`ARCHITECTURE.md` R7): on a Mac, in
private cloud macOS, or with xtool under WSL (which has an Apple licence question). Until it
is decided and implemented, step 7 of the user flow ("launch the game") is not possible.

## 4. User flow

```
First launch ──► "Import Your Game" ──► Files picker (folder or disc image)
                                              │
                                              ▼
                                   Verify (structure → version → completeness)
                         ┌────────────────────┼──────────────────────────┐
                         ▼                    ▼                          ▼
                   ✗ Incompatible       ⚠ Incomplete               ✓ Game data verified
                   (explains why;       (lists missing or          │
                   choose again)        damaged files; choose      ▼
                                        again or repair)     Game module present and
                                                             bound to this executable?
                                                    ┌──────────────┴──────────────┐
                                                    ▼                             ▼
                                          ✗ "Game module missing"          ✓ Ready → Play
                                          (explains the personal
                                          module step; links to guide)
```

Screens:

| Screen | Content |
|---|---|
| **Import Your Game** | What is needed (your own Xbox 360 copy of the supported edition, `SUPPORTED_GAME_DATA.md`), storage required, two choices: *Choose Game Folder* and *Choose Disc Image* |
| **Verifying** | Progress by phase and file, cancelable. Hashing ~7–8 GB takes minutes, and the screen says so |
| **Result** | One of the states in §6 with its message and next action |
| **Game Data** (in Settings) | Edition, version, title update, location, size, last verified; *Verify Again*, *Choose Again*, *Remove Game Data* |

## 5. Import and storage

### Sources the user can pick

| Source | Picker mode | Notes |
|---|---|---|
| Extracted game folder (contains `default.xex`) | Folder (`UIDocumentPickerViewController` for `.folder`) | First format to implement |
| Disc image (`.iso`, plain Xbox 360 game partition) | File | Read in place with `pread`, without extracting; as done by the MW 2005 mobile port |
| Title update package | File | Only if the supported version requires one (`SUPPORTED_GAME_DATA.md`) |

### Storage modes

| Mode | How | Pros | Cons |
|---|---|---|---|
| **Copy into the app** (default) | Copy to `Application Support/GameData/<edition-id>/`, excluded from iCloud and device backups (`isExcludedFromBackup`) | Reliable, fast, works offline, survives the source being moved | Uses ~7–8 GB of iPad storage; the app checks free space before copying |
| **Use in place** | Keep a **security-scoped bookmark** to the folder or image (`bookmarkData(options: .minimalBookmark)` / `startAccessingSecurityScopedResource`) | No duplicate storage, e.g. a USB-C drive | Fails when the drive is absent or the bookmark goes stale; external storage may be slower while streaming |

- Copying is atomic. It writes to a temporary folder, verifies, then renames, so an
  interrupted import never leaves half a game.
- **Re-selection (R7):**
  - A stale bookmark, a missing file or a failed hash puts the app in *Needs attention* with
    *Choose Again*.
  - Choosing a different copy re-verifies it from scratch.
  - Replacing the data never touches saves, which live separately in `Documents/Saves/`.
- Files are opened read-only. The app never modifies the user's original files.

## 6. Verification

Three phases, in order. Each failure has its own message (R5).

| Phase | Check | Failure state and message (examples) |
|---|---|---|
| 1. Structure | `default.xex` exists; the expected top-level folders exist; the image is a readable Xbox 360 game partition | **Not a game folder** — "This folder has no `default.xex`. Choose the folder that contains the game's files, or choose a disc image." |
| 2. Identity and version | Title ID read from the executable header (`45410961` expected); executable SHA-256 compared with the supported list; title update version | **Different game** — "This is not Need for Speed: Most Wanted (2012)." **Unsupported edition** — "This is edition X. Supported: Y. Your copy is not supported yet." **Needs title update** — "Version 1.0 found; title update N is required." |
| 3. Completeness | Every file in the edition's manifest: present, size matches, SHA-256 matches | **Incomplete** — "12 files are missing" (listed). **Damaged** — "3 files do not match the original" (listed, with suggestions such as re-copying or checking the disc) |

- The **manifest** is a list of relative paths, sizes and SHA-256 hashes per supported
  edition. It contains no game content. It is created once from the project owner's own
  legitimate copy (`ROADMAP.md` N1-c) and shipped in the app.
- **Quick checks** (structure, identity, sizes) run at every launch. **Full hashing** runs on
  import and on *Verify Again*.
- Results are written to the local log only. No telemetry, no network (R6).

States: `noGame` · `verifying` · `incompatible(reason)` · `incomplete(missing, damaged)` ·
`verifiedNoModule` · `moduleMismatch` · `ready` · `needsAttention(staleBookmark|missingSource)`.

## 7. What BYOG does and does not do about protection (R9)

- The app performs **no DRM bypass and no authentication circumvention**. It does not
  download games, keys, title updates or executables, and gives no instructions for
  obtaining them.
- Verification compares hashes. It does not alter files.
- **Open item:** Xbox 360 executables are stored in an encrypted and compressed container.
  ReXGlue's loader decodes that container to read the executable, at codegen time and again
  at run time. Whether this step is acceptable is the owner's decision (open since
  `NATIVE_PORT_RESEARCH.md` §7). It is **not** silently assumed to be acceptable. Nothing
  will be built that depends on it until it is decided.

## 8. Asset fidelity (R8)

- Assets are read in their original Xbox 360 formats and handed to the runtime unchanged.
- Any processing a GPU needs (e.g. converting a texture format the iPad's GPU cannot sample)
  is done losslessly where possible. Any lossy step needs an explicit, visible setting that
  is off by default, recorded in the verification screen.
- Note: the supported data is the **Xbox 360 edition's** data. It is the original asset set
  of that edition. It is not the PC edition's higher-resolution set (`FEASIBILITY.md`
  revision 3).

## 9. Privacy and repository rules

- Game data, disc images, executables, generated code and personal modules are never:
  - committed to the repository;
  - uploaded to GitHub Actions or any other server;
  - attached to issues or releases.
- `.gitignore` already blocks these file types.
- Logs exclude file contents and personal paths; exported logs are reviewed by the user
  before sharing.
- The manifest of hashes and sizes is the only game-derived information in the repository.
  It is factual metadata, not content.

## 10. Status

| Component | State |
|---|---|
| Import screen, picker, bookmarks | Not implemented |
| Verification engine | Not implemented |
| Manifest for the supported edition | Not created (needs the owner's copy, N1-c) |
| Game-module loader and binding check | Not implemented |
| Personal module build | Not designed beyond §3; build route undecided |
| Any of the above tested on an iPad | **No** |

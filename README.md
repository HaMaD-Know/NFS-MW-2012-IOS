# NFS MW 2012 iPad — native port research project

An unofficial research project investigating a **native ARM64 iPadOS port** of *Need for
Speed: Most Wanted* (2012), by static (ahead-of-time) recompilation of the Xbox 360 release
with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). The target is Apple Metal for
graphics and game controllers for input.

> **Status: toolchain proof (Milestone N1-a).** There is no port and no game code here. The
> iPad app in `App/` is a *platform test shell*: a spinning triangle drawn with Metal, game
> controller detection, logging and a memory reading. It is not the game.

## Rules this repository follows

- **No game files, ever.** No executables, data, disc images, title updates, textures,
  audio or video. Each user supplies their own legally owned copy.
- **No code generated from a game executable.** Recompiler output is derived from the
  publisher's code. It stays on the machine that produced it and is never committed,
  uploaded to CI or released. `.gitignore` enforces this.
- **No DRM circumvention**, and no help obtaining game files.
- Not affiliated with or endorsed by Electronic Arts or Criterion Games. *Need for Speed*
  and *Most Wanted* are trademarks of Electronic Arts Inc.

## What is here

| Path | What it is |
|---|---|
| `App/` | iPadOS test shell (Swift, SwiftUI, MetalKit, GameController); `project.yml` for XcodeGen |
| `.github/workflows/ios-shell.yml` | Builds the shell as an **unsigned arm64 IPA** on a GitHub macOS runner |
| `.github/workflows/rexglue-sdk.yml` | Builds the ReXGlue SDK from source at a pinned commit, with its tests, on Windows and macOS |
| `Tools/` | Small read-only analysis scripts (Python standard library only) |
| `Documentation/BUILD_GUIDE.md` | How to build and install the shell without a Mac |
| `Documentation/PUBLIC_STATUS.md` | What has and has not been demonstrated |

## Installing the test shell on an iPad (no Mac)

1. Open the latest successful **iPad test shell** run under *Actions* and download the
   `NFSMWShell-unsigned-ipa` artifact.
2. Sign and install it from Windows with a sideloading tool (Sideloadly, AltStore or
   SideStore) using your own Apple ID.
3. Open it with a controller connected. The overlay shows the frame rate, the controller
   and the memory available to the app. The log is in *Files › On My iPad › NFS MW Shell*.

## Licence

Not chosen yet. Until a licence file is added, all rights are reserved. Third-party
components keep their own licences.

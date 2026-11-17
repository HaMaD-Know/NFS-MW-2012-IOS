# Supported Game Data

Status 2026-11-17: **no game data is supported yet.** The table below is the *target*. An
edition becomes "supported" only after its manifest exists and an import plus a launch have
been tested on a real iPad.

## Target edition

| Field | Value | Status |
|---|---|---|
| Game | *Need for Speed: Most Wanted* (2012), Criterion Games / Electronic Arts | Target |
| Platform of the data | **Xbox 360** retail | Target |
| Title ID | `45410961` | From the Xenia compatibility tracker; to be confirmed from the owner's own executable |
| Region / media ID | **To be determined** from the owner's copy (Milestone N1-c) | Open |
| Executable (`default.xex`) SHA-256 | **To be determined** | Open |
| Title update required | **To be determined** (unknown whether the game had title updates) | Open |
| Manifest (paths, sizes, SHA-256) | Not created | Open |

Other regions and editions are supported only if they get their own manifest and are tested.
Recompiled code is bound to one exact executable, so every executable version is a separate
edition.

## Not supported

| Data | Reason |
|---|---|
| PC edition (`NFS13.exe`, PC bundles) | Different executable and platform; this project recompiles the Xbox 360 code |
| PlayStation 3, Wii U, PS Vita, mobile editions | Different platforms and executables |
| *Most Wanted* (2005) | A different game |
| Modified, cracked or repacked copies, prototypes, leaked builds | Not legitimate or not reproducible |
| Data downloaded from the internet | Not the user's own legally obtained copy |

## Accepted formats

| Format | Description | Status |
|---|---|---|
| Extracted game folder | A folder holding `default.xex` and the game's files exactly as on the disc's game partition | Planned first |
| Disc image | A plain `.iso` of the user's own disc, read in place | Planned |
| Title update | Only if the edition requires it | Planned, if needed |
| Games on Demand, compressed or converted images (`.god`, `.cci`, `.zar`, re-compressed `.iso`) | — | Not planned |

## Legally usable data

- **Your own copy**: a backup you made yourself of a retail Xbox 360 disc you own.
- How to make that backup depends on your hardware and on the law where you live. This
  project does not provide tools, links or instructions for obtaining game files.
- The app does not download, unlock or decrypt game data on your behalf. The executable's
  container decoding is an open question (`BYOG_DESIGN.md` §7).
- Your files stay on your devices. They are never uploaded by this project.

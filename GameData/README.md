# GameData

This folder is a placeholder. **No game file is ever stored in this repository.**

The app follows a Bring Your Own Game design (`Documentation/BYOG_DESIGN.md`):

- On the iPad, the user imports their own legally obtained Xbox 360 game data through the
  Files picker. The app copies it into its own storage or uses it in place, and verifies it
  against the supported edition (`Documentation/SUPPORTED_GAME_DATA.md`).
- Game data never goes into this repository, a CI run, an issue, a release or any server.
- Do not copy executables, bundles, audio, video, disc images, title updates or recompiled
  code here. `.gitignore` blocks common game file types as a safety net.
- The original PC installation on the developer's computer is treated as read-only and is
  not a supported data source.

# Build your personal NFS MW iPad app

This project is **Bring Your Own Game**. The public app contains no part of *Need for Speed:
Most Wanted* (2012). To play, you need your own legally obtained Xbox 360 copy of the game, and
you build your **personal app** from it once.

> **Status:** the port is still in development. It boots, plays the opening cutscene and starts
> driving, but it is not stable yet (see `Documentation/PUBLIC_STATUS.md`).

## Why there is a build step

The port is a *static recompilation*: the game's Xbox 360 code is translated into native iPad
code on a computer, ahead of time. iPadOS does not allow an app to translate and run code
by itself (no JIT), so the translation can't happen on the iPad.

The translated code is still the game's code, so it can't be in the public app either. Each
player builds it from their own copy, in their own private GitHub repository. Nobody else ever
sees it.

## What you need

- Your own Xbox 360 copy of *Need for Speed: Most Wanted* (2012) as a disc image (`.iso`) or
  extracted folder, on your iPad or a USB drive. This project does not provide games, keys or
  downloads, and does not tell you where to get them.
- An iPad with Apple silicon (M1 or newer recommended) and a way to install apps yourself
  (sideloading, e.g. LiveContainer, SideStore or AltStore).
- A free GitHub account. The build uses your account's free GitHub Actions minutes: the first
  build takes about 20 macOS minutes (about 200 of your monthly free minutes, because macOS
  minutes count 10×). Later builds with the same files take about 5 macOS minutes.
- An Xbox or other supported game controller.

## Steps

1. **Install the public app** (NFS MW Shell) on your iPad.
2. In the app, tap **Choose Disc Image** (or **Choose Game Folder**) and pick your copy. The app
   reads it in place and checks that it is the right game.
3. Tap **Export game code**. The app copies only two files out of your copy into
   *Files › NFS MW Shell › Export*: `default.xex` and `hawaii.xex.dll`.
4. On GitHub, create a **new private repository** (*Repositories › New*, choose **Private**).
5. In that repository:
   - create the file `.github/workflows/build-my-game.yml` with the contents of
     [`Kit/build-my-game.yml`](build-my-game.yml);
   - upload the two exported files into a folder named `game` (*Add file › Upload files*, then
     put `game/` in front of the names, or create `game/README.md` first and upload into it).
6. Uploading the files starts the build. You can also start it in *Actions › Build my NFS MW
   iPad app › Run workflow*. When it finishes, download the artifact
   **NFSMW-game-private-ipa** from the run's page (it is kept for 30 days).
7. Install that IPA on your iPad the same way you installed the public app. It replaces the
   public app and keeps your imported game and settings.
8. Open the app. Your disc image is still chosen; tap **▶ Play**.

The app checks that the exported files are the same ones your personal app was built from. If
you change to another copy of the game, export again and build again.

## Rules

- Keep the repository **private**. The build refuses to run in a public repository.
- Never share the two game files, the translated code, the build artifacts or the personal IPA.
  Don't attach them to issues or post them anywhere. They are derived from your copy of the game.
- After the build, you can delete the two files from *Files › NFS MW Shell › Export*. Play needs
  them again, so export again before playing.

## If something goes wrong

| Message | What to do |
|---|---|
| *Game files missing* | Both `game/default.xex` and `game/hawaii.xex.dll` must be in the repository's `game` folder. |
| *Wrong game file* | The `default.xex` is from another game. Export again from your NFS MW (2012) copy. |
| *This repository is public* | Make it private (*Settings › General › Change visibility*), then run again. |
| *The job was not started because recent account payments have failed or your spending limit needs to be increased* | Your account's free Actions minutes are used up. They reset each month, or you can set an Actions budget under *Settings › Billing and licensing*. |
| In the app: *This app was built for another copy* | The exported files differ from the ones the app was built from. Export from the same copy, or build again. |

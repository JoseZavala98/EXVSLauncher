# EXVS Launcher

A modern launcher for EXVSIB that replaces SnowLauncher. It's one exe (`EXVS Launcher.exe`) that goes in the game folder.

- **Play:** Client, Live Monitor, or both. Quick window and resolution pickers, card and server at a glance, and status for the game, N2N and the local server.
- **Game:** card (chip ID and access code), server, serial, region, opening skip, display per Client/LM, network adapter and fixed IP.
- **Controls:** keyboard for Client and LM (click a box, then press a key), and controller mapping through DirectInput button capture.
- **Network:** N2N (`edge.exe`) with a live log, plus the local card server (`Server\Server.exe` or `Server.exe`).
- **Streamer mode** (Settings > Launcher > Privacy): hides the card server address and the access code in the launcher.
- **Settings:** background picture, mod sync before playing (`EXVSIB_Mod_Manager.exe --sync`), and install/remove of the bundled EXVS Overlay.

## Compatibility with SnowLauncher
Settings live in `SnowLauncher.ini` under the same keys SnowLauncher uses, so both launchers and the overlay see the same values.
Options that only this launcher has go in `EXVSLauncher.ini`. On Play it writes the game's files the same way SnowLauncher does:
- `config.ini`: Client settings. `A/B/C` are composed from `ButtonA/AB/AC/ABC` and so on.
- `lmnfig.ini`: Live Monitor settings.
- `card.ini`: card name and access code.

It then starts `run_iboost_CLIENT_mode_v4.exe` / `run_iboost_LM_mode_v4.exe`, or the game exe directly if those are missing.
Fullscreen (cover taskbar) sets the loader's native `BorderlessWindow=True`. Other window sizes are applied by the launcher during
the first 90 s, unless the overlay is installed (the overlay keeps the window itself).

N2N command line (taken from SnowLauncher's strings): `edge -l host:port -c community [-S1] [--no-port-forwarding] [-a ip] [-E] -d <TAP> -I <name> [extra]`.
After it connects, the TAP adapter's IPv4 metric is set to 1.

## Install
1. Download `EXVS Launcher.exe` from the releases (or build it, see below).
2. Put it in the game folder, next to `vsac29_Release.exe` (the folder where SnowLauncher lives).
3. Run it. It asks for administrator rights, like SnowLauncher, and reads your existing `SnowLauncher.ini`.

To go back to SnowLauncher, just run SnowLauncher again: the settings are shared.
The in-game overlay is installed or removed from **Settings > In-game overlay**.

## Build
Clone with submodules (webview is a git submodule in `third_party/`):

```bash
git clone --recursive <repo url>
# or, in an existing clone: git submodule update --init
```

Requirements (Windows, Git Bash):
- Python 3 with the pip packages `ziglang` (used as the C++ compiler through `py -m ziglang`) and `Pillow`.
- A built [EXVS Overlay](https://github.com/furshow/EXVSOverlay), because its DLL is embedded in the exe.
  By default the build looks for it in the sibling folder `../EXVSOverlay`; set `EXVS_OVERLAY_DIR` to use another checkout.

```bash
(cd ../EXVSOverlay && bash build.sh)
bash build.sh                      # or: EXVS_OVERLAY_DIR=/path/to/EXVSOverlay bash build.sh
```

The result is `build/EXVS Launcher.exe`.

- UI: `ui/index.html`, embedded at build time by `tools/make_assets.py`. Opened in a normal browser, it runs against a mock backend.
- Host: `src/main.cpp`, using webview 0.12 (MIT) and WebView2 SDK headers. JSON by nlohmann/json (MIT).
- `--selftest <dir> <out.json>` exercises the ini logic on a copy of a game folder.
- `--uitest <dir> <out.json>` loads the real UI in a hidden window and reports.

Both test modes are meant for a build without the manifest (the release exe asks for administrator rights, like SnowLauncher).

## Updates
The launcher updates itself from the releases of this repo (src/update.h). When it opens it asks the GitHub API for the
latest release (turn this off in **Settings > Launcher > Check for updates at start**, or set EXVS_NO_UPDATE=1).
If the tag is newer than VERSION in src/main.cpp, the Play page offers it. Update downloads the .exe next to the
running one, checks its size and SHA-256 (GitHub's asset digest), renames the running exe to EXVS Launcher.old.exe,
puts the new one in its place and restarts. The new version bundles its EXVS Overlay, so the overlay update follows.

Drafts and pre-releases are ignored.

## Releases
Branching follows git flow: work goes to `develop` (feature branches from it), a `Release/x.y.z` branch is cut from
`develop` for each version and merged into `main` (and back into `develop`); fixes to a released version go in
`hotfix/x.y.z` from `main`.

To publish a version:
1. If the overlay changed, release EXVS Overlay first (and publish its release), then put its version in
   `overlay-version.txt`.
2. On the `Release/x.y.z` branch: set VERSION in src/main.cpp and add a `## [x.y.z]` section to CHANGELOG.md.
3. Merge it into `main`, then tag main: `git tag vx.y.z && git push origin vx.y.z`.
4. `.github/workflows/build-release.yml` builds the exe (with the overlay DLL downloaded from its published release)
   and creates a draft release with `EXVS Launcher.exe` and the CHANGELOG notes. Review it and press **Publish**:
   from then on the launcher's updater offers it. The release notes are shown in the update window.

## Support
If this launcher is useful to you, you can support its development. It's optional, and everything stays free.

[![Support me on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/N3L02AMSOT)

## License
MIT, see [LICENSE](LICENSE). Third-party code is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Fan-made tool, not affiliated with Bandai Namco or Sunrise. No game files are included.

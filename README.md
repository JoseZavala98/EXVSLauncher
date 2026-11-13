# EXVS Launcher

A modern launcher for EXVSIB that replaces SnowLauncher. It's one exe (`EXVS Launcher.exe`) that goes in the game folder.

- **Play:** Client, Live Monitor, or both. Quick window and resolution pickers, card and server at a glance, and status for the game, N2N and the local server.
- **Game:** card (chip ID and access code), server, serial, region, opening skip, display per Client/LM, network adapter and fixed IP.
- **Controls:** keyboard for Client and LM (click a box, then press a key), and controller mapping through DirectInput button capture.
- **Network:** N2N (`edge.exe`) with a live log, plus the local card server (`Server\Server.exe` or `Server.exe`).
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
- A built [EXVS Overlay](https://github.com/JoseZavala98/EXVSOverlay), because its DLL is embedded in the exe.
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

## License
MIT, see [LICENSE](LICENSE). Third-party code is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Fan-made tool, not affiliated with Bandai Namco or Sunrise. No game files are included.

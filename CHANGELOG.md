# Changelog

All notable changes to EXVS Launcher. The release workflow (.github/workflows/build-release.yml) uses the
section of the released version as the release notes.

## [1.0.0] - 2026-11-14
First public release.

- A modern replacement for SnowLauncher: one exe in the game folder. It shares `SnowLauncher.ini`, so you can go
  back to SnowLauncher at any time.
- **Play:** Client, Live Monitor, or both, with quick window and resolution pickers and the status of the game,
  N2N and the local server.
- **Settings:** a game-menu style screen (Graphics, Gameplay, Controls, Account, Advanced, Launcher) with
  keyboard and controller navigation and auto-save.
  - All SnowLauncher resolutions and window sizes, with a recommended resolution for your monitor.
  - Keyboard for Client and Live Monitor, and controller mapping for both pads (XInput or DirectInput, with
    the game's own button layout or a custom one).
  - Card name, access code, card server (your own address, or Local with `Server.exe`), serial, region and
    opening skip.
- **Streamer mode:** hides the card server address and the access code.
- **Online:** N2N virtual LAN with a live log, and the local card server.
- **Mods:** opens the EXVSIB Mod Manager and can sync mods before playing.
- Bundles **EXVS Overlay 1.0.0** and installs, updates or removes it from Settings.
- Updates itself from the releases of this repo.
- English, Spanish, Simplified Chinese and Japanese.

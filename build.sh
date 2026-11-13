#!/usr/bin/env bash
# Builds build/EXVS Launcher.exe with zig (pip package "ziglang") and Pillow.
# The EXVS Overlay must be built first (EXVSOverlay/build.sh): its DLL is embedded.
set -euo pipefail
cd "$(dirname "$0")"
ZIG="py -m ziglang"
mkdir -p build/gen
# EXVS Overlay checkout: $EXVS_OVERLAY_DIR, or the sibling folder ../EXVSOverlay
export EXVS_OVERLAY_DIR="${EXVS_OVERLAY_DIR:-../EXVSOverlay}"
test -f "$EXVS_OVERLAY_DIR/build/xinput9_1_0.dll" || { echo "build EXVSOverlay first ($EXVS_OVERLAY_DIR)"; exit 1; }
cp "$EXVS_OVERLAY_DIR/build/xinput9_1_0.dll" build/gen/xinput9_1_0.dll
py -3.14 tools/make_assets.py .

$ZIG c++ -target x86_64-windows-gnu -O2 -std=c++17 -municode -Wl,--subsystem,windows \
  -Ithird_party/compat -Ithird_party/webview/core/include -Ithird_party/webview2/include -Ithird_party/json -Ibuild/gen \
  src/main.cpp src/app.rc -o "build/EXVS Launcher.exe" \
  -ladvapi32 -lole32 -lshell32 -lshlwapi -luser32 -lversion -ldwmapi -lcomdlg32 -liphlpapi -lws2_32 -ldinput8 -ldxguid
rm -f build/*.pdb build/*.lib 2>/dev/null || true
ls -la build/*.exe

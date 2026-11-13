"""Build-time assets for EXVS Launcher.

    py -3.14 tools/make_assets.py <launcher dir>

- ui/index.html with <!--LOGO--> replaced by the logo SVG
  -> build/gen/ui.html (for previewing in a browser)
  -> build/gen/ui_html.h (C++ raw string, split in chunks to stay under compiler limits)
- build/gen/app.ico, logo.svg, logo_preview.png from tools/logo.py (needs Pillow)
"""
import os
import sys

root = sys.argv[1]
gen = os.path.join(root, "build", "gen")
os.makedirs(gen, exist_ok=True)
sys.path.insert(0, os.path.join(root, "tools"))
import logo  # noqa: E402

html = open(os.path.join(root, "ui", "index.html"), encoding="utf-8").read()
html = html.replace("<!--LOGO-->", logo.svg())
i18n = open(os.path.join(root, "ui", "i18n.js"), encoding="utf-8").read()
assert "</script" not in i18n
html = html.replace('<script src="i18n.js"></script>', "<script>\n" + i18n + "\n</script>")
open(os.path.join(gen, "ui.html"), "w", encoding="utf-8", newline="\n").write(html)

DELIM = "EXVSUI"
assert ")" + DELIM + '"' not in html
chunks = [html[i:i + 12000] for i in range(0, len(html), 12000)]
with open(os.path.join(gen, "ui_html.h"), "w", encoding="utf-8", newline="\n") as f:
    f.write("// generated from ui/index.html by tools/make_assets.py\n#pragma once\n#include <string>\n")
    f.write("static const std::string UI_HTML = std::string()\n")
    for c in chunks:
        f.write(f'    + std::string(R"{DELIM}({c}){DELIM}")\n')
    f.write(";\n")

import re  # noqa: E402
# EXVS Overlay checkout: $EXVS_OVERLAY_DIR, or the sibling folder ../EXVSOverlay
ov_dir = os.environ.get("EXVS_OVERLAY_DIR") or os.path.join(root, "..", "EXVSOverlay")
ov = open(os.path.join(ov_dir, "src", "menu.h"), encoding="utf-8").read()
ov_ver = re.search(r'#define OVERLAY_VERSION "([^"]+)"', ov).group(1)
with open(os.path.join(gen, "overlay_version.h"), "w", encoding="utf-8", newline="\n") as f:
    f.write(f'#pragma once\n#define OVERLAY_BUNDLED_VERSION "{ov_ver}"\n')

open(os.path.join(gen, "logo.svg"), "w", encoding="utf-8").write(logo.svg())
img = logo.png(256)
img.save(os.path.join(gen, "logo_preview.png"))
img.save(os.path.join(gen, "app.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("assets ok:", len(html), "bytes of html in", len(chunks), "chunks")

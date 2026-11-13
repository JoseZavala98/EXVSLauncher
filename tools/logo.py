"""EXVS Launcher logo: a flat app mark, rendered as SVG (UI) and with Pillow (icon / PNG).

    py -3.14 tools/logo.py <out dir>   -> logo.svg, logo_preview.png, app.ico, logo_full.png

Navy gradient tile (#1b4fa8 -> #0d1f45, like the first sidebar badge) with custom "VS" letters: angular,
slanted, with chamfered corners. The letters are polygons, so they don't depend on any font.
"""
import os
import sys

S = 256
TILE_A, TILE_B = "#1b4fa8", "#0d1f45"   # diagonal gradient, top-left -> bottom-right
RIM = (255, 255, 255, 31)               # 1px inner highlight, 12 % white
GLYPH = "#ffffff"
RADIUS = 62
SLANT = 0.24                            # italic shear (x shift per unit of height)

_V = [(38, 82), (66, 82), (84, 140), (102, 82), (130, 82), (98, 174), (70, 174)]
_S = [(152, 82), (218, 82), (218, 104), (162, 104), (162, 117), (204, 117), (218, 131), (218, 160), (204, 174),
      (136, 174), (136, 152), (192, 152), (192, 139), (150, 139), (136, 125), (136, 96)]


def _letters():
    """Shear both letters to the right and centre them in the tile."""
    out = []
    for poly in (_V, _S):
        out.append([(x + (174 - y) * SLANT, y) for x, y in poly])
    xs = [x for p in out for x, _ in p]
    dx = S / 2 - (min(xs) + max(xs)) / 2
    return [[(round(x + dx, 1), y) for x, y in p] for p in out]


LETTERS = _letters()


def svg():
    p = lambda pts: " ".join(f"{x},{y}" for x, y in pts)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {S} {S}">'
            f'<defs><linearGradient id="exvs-tile" x1="0" y1="0" x2="1" y2="1">'
            f'<stop offset="0" stop-color="{TILE_A}"/><stop offset="1" stop-color="{TILE_B}"/></linearGradient></defs>'
            f'<rect width="{S}" height="{S}" rx="{RADIUS}" fill="url(#exvs-tile)"/>'
            f'<rect x="3" y="3" width="{S - 6}" height="{S - 6}" rx="{RADIUS - 3}" fill="none" stroke="rgba(255,255,255,0.12)" stroke-width="4"/>'
            + "".join(f'<polygon points="{p(l)}" fill="{GLYPH}"/>' for l in LETTERS) + "</svg>")


def png(size):
    from PIL import Image, ImageDraw
    k = 4
    big = S * k
    rgb = lambda h: tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))
    a, b = rgb(TILE_A), rgb(TILE_B)
    grad = Image.new("RGBA", (big, big))
    gp = grad.load()
    for y in range(big):
        for x in range(0, big, 4):   # diagonal gradient, 4 px steps are invisible after downscaling
            t = (x + y) / (2 * big)
            c = tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3)) + (255,)
            for xx in range(x, min(x + 4, big)):
                gp[xx, y] = c
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, big - 1, big - 1], radius=RADIUS * k, fill=255)
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    img.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([3 * k, 3 * k, big - 1 - 3 * k, big - 1 - 3 * k], radius=(RADIUS - 3) * k, outline=RIM, width=4 * k)
    for l in LETTERS:
        d.polygon([(x * k, y * k) for x, y in l], fill=rgb(GLYPH) + (255,))
    return img.resize((size, size), Image.LANCZOS)


def png_full(width=1200):
    """Mark + "EXVS Launcher" wordmark side by side, for sharing."""
    from PIL import Image, ImageDraw, ImageFont
    k = 4
    W, H = 900 * k, 256 * k
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    mark = png(256 * k)
    img.paste(mark, (0, 0), mark)
    d = ImageDraw.Draw(img)
    try:
        f = ImageFont.truetype(os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts", "segoeuib.ttf"), 120 * k)
    except OSError:
        f = ImageFont.load_default()
    d.text((300 * k, 128 * k), "EXVS Launcher", font=f, fill=(244, 247, 252, 255), anchor="lm")
    bbox = d.textbbox((300 * k, 128 * k), "EXVS Launcher", font=f, anchor="lm")
    img = img.crop((0, 0, min(W, bbox[2] + 20 * k), H))
    return img.resize((width, int(width * img.height / img.width)), Image.LANCZOS)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, "logo.svg"), "w", encoding="utf-8").write(svg())
    b = png(256)
    b.save(os.path.join(out, "logo_preview.png"))
    b.save(os.path.join(out, "app.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    png_full().save(os.path.join(out, "logo_full.png"))
    print("logo ok")

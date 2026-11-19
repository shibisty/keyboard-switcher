"""Generates res/app.ico and MSIX logo assets. Requires Pillow: pip install pillow"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
S = 1024

def draw_icon(size=S, plate=True):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if plate:
        # rounded square with vertical gradient
        grad = Image.new("RGBA", (S, S))
        gd = ImageDraw.Draw(grad)
        for y in range(S):
            t = y / S
            c = (int(0x3b + (0x1d - 0x3b) * t), int(0x82 + (0x4e - 0x82) * t), int(0xf6 + (0xd8 - 0xf6) * t), 255)
            gd.line([(0, y), (S, y)], fill=c)
        mask = Image.new("L", (S, S), 0)
        ImageDraw.Draw(mask).rounded_rectangle([40, 40, S - 40, S - 40], radius=220, fill=255)
        img.paste(grad, (0, 0), mask)
    key_bg = (255, 255, 255, 255)
    key_fg = (0x1d, 0x4e, 0xd8, 255) if plate else (0, 0, 0, 0)
    # keyboard body
    bx0, by0, bx1, by1 = 170, 330, S - 170, S - 300
    d.rounded_rectangle([bx0, by0, bx1, by1], radius=60, fill=key_bg)
    # keys: 3 rows
    kw, gap = 64, 22
    rows = [(by0 + 50, 7, 0), (by0 + 50 + kw + gap, 6, 0)]
    for y, n, off in rows:
        total = n * kw + (n - 1) * gap
        x = (S - total) // 2 + off // 2
        for i in range(n):
            d.rounded_rectangle([x, y, x + kw, y + kw], radius=14, fill=key_fg)
            x += kw + gap
    sy = by0 + 50 + 2 * (kw + gap)
    d.rounded_rectangle([bx0 + 170, sy, bx1 - 170, sy + kw], radius=14, fill=key_fg)
    # switch arrows above the keyboard
    ay = 230
    d.line([(330, ay), (650, ay)], fill=key_bg, width=34)
    d.polygon([(700, ay), (630, ay - 50), (630, ay + 50)], fill=key_bg)
    return img.resize((size, size), Image.LANCZOS)

def main():
    base = draw_icon(256)
    base.save(ROOT / "res" / "app.ico", sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (256, 256)])
    draw_icon(256).save(ROOT / "res" / "app.png")                      # window icon, Linux
    draw_icon(1024).save(ROOT / "res" / "app.icns", sizes=[(16, 16), (32, 32), (64, 64), (128, 128),
                                                            (256, 256), (512, 512), (1024, 1024)])  # macOS
    a = ROOT / "packaging" / "Assets"
    a.mkdir(parents=True, exist_ok=True)
    draw_icon(50).save(a / "StoreLogo.png")
    draw_icon(150).save(a / "Square150x150Logo.png")
    draw_icon(44).save(a / "Square44x44Logo.png")
    for t in (16, 24, 32, 48, 256):
        draw_icon(t).save(a / f"Square44x44Logo.targetsize-{t}.png")
        draw_icon(t).save(a / f"Square44x44Logo.targetsize-{t}_altform-unplated.png")
    wide = Image.new("RGBA", (310, 150), (0, 0, 0, 0))
    wide.paste(draw_icon(150), (80, 0))
    wide.save(a / "Wide310x150Logo.png")

if __name__ == "__main__":
    main()

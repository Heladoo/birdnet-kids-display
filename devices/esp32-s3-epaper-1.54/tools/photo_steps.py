"""Show each step of the photo pipeline side by side, to review crops and tuning.

Usage: python3 photo_steps.py out.png photo.jpg[@x,y] [photo2.jpg[@x,y] ...]
  @x,y = where the bird is in the photo, as fractions (default 0.5,0.45).

Columns: original (cropped) | prepared grayscale | panel, 1-bit Atkinson |
1-bit Floyd-Steinberg | 4 greys (experimental). Scaled 2x with hard pixels.
"""
import sys

from PIL import Image, ImageDraw, ImageFont, ImageOps

from epaper_image import to_1bit, to_4gray, to_gray

BOX = (200, 108)
S = 2
GAP = 8
LABELS = ["original", "grayscale", "panel: Atkinson", "Floyd-Steinberg", "4 greys (exp.)"]


def main():
    out, specs = sys.argv[1], sys.argv[2:]
    cw, ch = BOX[0] * S, BOX[1] * S
    sheet = Image.new("RGB", (5 * (cw + GAP), 24 + len(specs) * (ch + GAP)), "white")
    d = ImageDraw.Draw(sheet)
    f = ImageFont.load_default(16)
    for c, label in enumerate(LABELS):
        d.text((c * (cw + GAP) + 4, 4), label, font=f, fill="black")
    for r, spec in enumerate(specs):
        path, _, at = spec.partition("@")
        focus = tuple(float(v) for v in at.split(",")) if at else (0.5, 0.45)
        src = Image.open(path)
        gray = to_gray(src, BOX, focus)
        color = ImageOps.fit(ImageOps.exif_transpose(src).convert("RGB"), BOX, Image.LANCZOS, centering=focus)
        for c, im in enumerate([color, gray, to_1bit(gray), to_1bit(gray, "floyd"), to_4gray(gray)]):
            im = im.convert("RGB").resize((cw, ch), Image.NEAREST)
            sheet.paste(im, (c * (cw + GAP), 24 + r * (ch + GAP)))
    sheet.save(out)


if __name__ == "__main__":
    main()

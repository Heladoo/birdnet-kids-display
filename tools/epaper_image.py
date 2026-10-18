"""Turn a bird photo into what the 200x200 black/white e-paper can show.

The photo itself is manipulated, never redrawn:
  crop on the bird -> grayscale -> local contrast + sharpen -> lighten the
  background -> dither to black/white dots (or to 4 greys, experimental).
"""
from PIL import Image, ImageChops, ImageFilter, ImageOps


def to_gray(img, size, focus=(0.5, 0.45)):
    """Crop to `size` around `focus` (fractions of the photo, where the bird
    is) and prepare a grayscale picture tuned for e-paper."""
    img = ImageOps.exif_transpose(img).convert("RGB")
    img = ImageOps.fit(img, size, Image.LANCZOS, centering=focus)
    g = img.convert("L")
    g = ImageOps.autocontrast(g, cutoff=1)
    # Local contrast: subtract a blurred copy so feathers and eyes stand out
    # against soft backgrounds (an "unsharp mask" with a big radius), then a
    # small sharpen for edges.
    g = g.filter(ImageFilter.UnsharpMask(radius=8, percent=60, threshold=0))
    g = g.filter(ImageFilter.UnsharpMask(radius=1, percent=120, threshold=1))
    # Lift the mid-tones: e-paper dots look darker than the same grey on a screen.
    g = g.point(lambda v: int(255 * (v / 255) ** 0.8))
    return fade_background(g)


def fade_background(g, strength=0.65):
    """Bird photos have a sharp bird on a blurred background. Measure local
    detail and fade the smooth (out-of-focus) areas toward white, so the
    dithered background becomes light, quiet dots instead of noise."""
    detail = ImageChops.difference(g, g.filter(ImageFilter.GaussianBlur(2)))
    detail = detail.filter(ImageFilter.GaussianBlur(6))
    detail = ImageOps.autocontrast(detail, cutoff=2).point(lambda v: min(255, v * 2))
    faded = g.point(lambda v: int(v + (255 - v) * strength))
    return Image.composite(g, faded, detail)


def _diffuse(gray, levels, kernel, divisor):
    w, h = gray.size
    flat = gray.tobytes()
    px = [list(flat[y * w:(y + 1) * w]) for y in range(h)]
    step = 255 / (levels - 1)
    out = Image.new("L", (w, h))
    o = out.load()
    for y in range(h):
        row = px[y]
        for x in range(w):
            old = row[x]
            new = int(min(levels - 1, max(0, round(old / step))) * step)
            o[x, y] = new
            err = old - new
            for dx, dy, k in kernel:
                xx, yy = x + dx, y + dy
                if 0 <= xx < w and yy < h:
                    px[yy][xx] += err * k / divisor
    return out


ATKINSON = [(1, 0, 1), (2, 0, 1), (-1, 1, 1), (0, 1, 1), (1, 1, 1), (0, 2, 1)]
FLOYD = [(1, 0, 7), (-1, 1, 3), (0, 1, 5), (1, 1, 1)]


def to_1bit(gray, method="atkinson"):
    """Black/white dots, the way the panel shows them. Atkinson passes on only
    6/8 of the error, so light areas stay clean white (better for e-paper)."""
    kernel, div = (ATKINSON, 8) if method == "atkinson" else (FLOYD, 16)
    return _diffuse(gray, 2, kernel, div).convert("1", dither=Image.NONE)


def to_4gray(gray):
    """4 grey levels: needs a custom waveform on the panel (experimental)."""
    return _diffuse(gray, 4, FLOYD, 16)

"""Turn a bird photo into what the 200x200 black/white e-paper can show.

The photo itself is manipulated, never redrawn:
  crop on the bird -> grayscale -> local contrast + sharpen -> lighten the
  background -> dither to black/white dots (or to 4 greys, experimental).
"""
from PIL import Image, ImageChops, ImageFilter, ImageOps

_session = None


def cut_out(img, max_side=1024):
    """Cut the bird out of its background with the U2-Net segmentation model
    (the `rembg` package; runs locally on the PC, ~1 s per photo). Returns an
    RGBA image (bird opaque, background transparent), or None if the cut looks
    wrong (nothing found, or nearly the whole photo)."""
    global _session
    from rembg import new_session, remove
    import numpy as np
    if _session is None:
        _session = new_session("u2net")
    img = ImageOps.exif_transpose(img).convert("RGB")
    img.thumbnail((max_side, max_side), Image.LANCZOS)
    rgba = remove(img, session=_session)
    frac = (np.asarray(rgba.split()[3]) > 128).mean()
    return rgba if 0.02 <= frac <= 0.90 else None


def to_gray_cutout(rgba, size, pad=0.06, gamma=1.0, outline=True, lift=0, clean=0.0):
    """Grayscale picture of a cut-out bird for e-paper: crop tight to the bird,
    scale it up to fill `size` (proportions kept), clean white background, thin
    outline. The background fade is skipped (the background is gone); contrast
    is measured on the bird alone.

    Per-species tuning (processing.json, used by build_pack.py):
      gamma < 1 lightens the mid-tones; lift (0..100) raises the black level, so very dark plumage
      (a black head) still shows texture in the dither instead of one black blob; clean (e.g. 0.03)
      deletes cut-out specks smaller than that fraction of the bird's main piece."""
    if clean:
        import numpy as np
        from scipy import ndimage

        a = np.asarray(rgba.split()[3]) > 128
        lab, n = ndimage.label(a)
        if n > 1:
            sizes = np.bincount(lab.ravel())
            keep = sizes >= clean * sizes[1:].max()
            keep[0] = False
            a2 = keep[lab]
            rgba = rgba.copy()
            rgba.putalpha(Image.fromarray((a2 * 255).astype("uint8")))
    alpha = rgba.split()[3].point(lambda v: 255 if v > 128 else 0)
    x0, y0, x1, y1 = alpha.getbbox()
    p = int(pad * max(x1 - x0, y1 - y0))
    box = (max(0, x0 - p), max(0, y0 - p), min(rgba.width, x1 + p), min(rgba.height, y1 + p))
    rgba = rgba.crop(box)
    mask = rgba.split()[3].filter(ImageFilter.MaxFilter(3))  # keep the feather edge
    white = Image.new("RGB", rgba.size, (255, 255, 255))
    white.paste(rgba.convert("RGB"), mask=mask)
    white = ImageOps.contain(white, size, Image.LANCZOS)
    mask = ImageOps.contain(mask, size, Image.LANCZOS)
    g = white.convert("L")
    g = ImageOps.autocontrast(g, cutoff=1, mask=mask.point(lambda v: 255 if v > 128 else 0))
    g = g.filter(ImageFilter.UnsharpMask(radius=6, percent=80, threshold=0))
    g = g.filter(ImageFilter.UnsharpMask(radius=1, percent=120, threshold=1))
    g = g.point(lambda v: int(255 * (v / 255) ** gamma))
    if lift:
        g = g.point(lambda v: lift + v * (255 - lift) // 255)
    bg = Image.new("L", g.size, 255)
    g = Image.composite(g, bg, mask)
    if outline:  # a thin black line around the bird, so the shape reads at a glance
        m = mask.point(lambda v: 255 if v > 128 else 0)
        edge = ImageChops.subtract(m.filter(ImageFilter.MaxFilter(3)), m.filter(ImageFilter.MinFilter(3)))
        g = Image.composite(Image.new("L", g.size, 0), g, edge)
    canvas = Image.new("L", size, 255)
    canvas.paste(g, ((size[0] - g.width) // 2, (size[1] - g.height) // 2))
    return canvas


def to_gray(img, size, focus=(0.5, 0.45), fit=True):
    """Prepare a grayscale picture tuned for e-paper, `size` pixels.

    fit=True (default): the whole photo at its own proportions, as large as it
    goes inside `size`, centred on white. Nothing is cut off, so the bird is
    never cropped; `focus` is ignored.
    fit=False: crop to `size` around `focus` (fractions of the photo, where the
    bird is)."""
    img = ImageOps.exif_transpose(img).convert("RGB")
    if fit:
        img = ImageOps.contain(img, size, Image.LANCZOS)
    else:
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
    g = fade_background(g)
    if g.size != tuple(size):
        canvas = Image.new("L", size, 255)
        canvas.paste(g, ((size[0] - g.width) // 2, (size[1] - g.height) // 2))
        g = canvas
    return g


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

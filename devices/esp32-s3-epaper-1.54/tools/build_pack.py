"""Build the bird-picture pack for the e-paper display.

Usage: python3 build_pack.py [--data data.json] [--expert expert-data.json]
       [--photos DIR] [--no-download] [--out ../out] [--firmware ../firmware]
       [--no-cutout] [--recut] [--no-assets]

The bird is cut out of its photo (U2-Net via `pip install "rembg[cpu]"`, local,
~1 s per photo; the model, 176 MB, is downloaded on first use) and shown large on
a clean white background. Cut-outs are cached in cache/cut/; --recut redoes them
(after changing a photo), --no-cutout uses the old crop + background fade
(also the fallback when the cut looks wrong). --no-assets leaves ui_assets.h alone.

--data / --expert default to the published site files (SITE below), downloaded fresh
into cache/ on every run (an older copy in cache/ is used if the download fails).

For every species in data.json and expert-data.json it writes one card,
firmware/data/cards/<sci_slug>.bin (this folder is what `pio run -t uploadfs`
puts on the ESP's LittleFS), and it regenerates firmware/src/ui_assets.h (Hebrew
phrases, digit fonts and icons, compiled into the firmware). It also writes
out/contact.png (every photo as the panel shows it) and out/report.txt.

Photo for a species, first match wins:
  1. overrides/<slug>.jpg|png|webp   a hand-picked photo (next to this script's parent)
  2. --photos DIR/<slug>.jpg|png|webp   photos that already exist on this PC
  3. cache/<slug>.img                a previous download
  4. the `image` URL from data.json, downloaded into cache/ (unless --no-download)
No photo: the card gets an empty photo box and the species is listed in the report.
crops.json ({"<Scientific name>": [x, y]}) sets where the bird is in the photo,
as fractions; the default is the centre, slightly high.

Card file format "BKC1" (all bitmaps: rows padded to whole bytes, MSB first,
bit 1 = black ink, which is what Adafruit_GFX drawBitmap() takes):
  0   'B' 'K' 'C' '1'
  4   u8 he_big width, u8 en width, u8 he_small width, u8 0
  8   photo      200 x 108                       2700 bytes
      he_big     <w> x 24  Hebrew name, large    centered on screen A
      en         <w> x 14  English name          centered on screen A
      he_small   <w> x 16  Hebrew name, small    right-aligned on screen D
"""
import io
import json
import re
import sys
import urllib.request
from pathlib import Path

from PIL import Image, ImageDraw, ImageOps

from epaper_image import cut_out, to_1bit, to_gray, to_gray_cutout
from mockups import ICONS, W, font, text

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

PHOTO = (200, 108)
HE_BIG_H, EN_H, HE_SMALL_H = 24, 14, 16
UA = "BirdnetKidsDisplay/1.0 (https://github.com/Heladoo/birdnet-kids-display)"
SITE = "https://raw.githubusercontent.com/Heladoo/Birdnet_kids/main/"


def arg(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def site_json(name, explicit, cache):
    """Path of a site file: the one given on the command line, or the published copy."""
    if explicit:
        return Path(explicit)
    path = cache / name
    try:
        data = urllib.request.urlopen(urllib.request.Request(SITE + name, headers={"User-Agent": UA}), timeout=30).read()
        cache.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    except Exception as e:
        if not path.exists():
            raise SystemExit(f"cannot download {SITE + name}: {e}")
        print(f"download of {name} failed ({e}); using the copy in cache/", file=sys.stderr)
    return path


def slug(sci):
    return re.sub(r"[^a-z0-9]+", "_", sci.lower()).strip("_")


# ------------------------------------------------------------------ bitmaps

def pack_bits(img):
    """1-bit image -> bytes, rows padded to bytes, bit 1 = black."""
    img = img.convert("1")
    w, h = img.size
    rb = (w + 7) // 8
    out = bytearray(rb * h)
    px = img.load()
    for y in range(h):
        for x in range(w):
            if px[x, y] == 0:
                out[y * rb + x // 8] |= 0x80 >> (x % 8)
    return bytes(out)


def render_strip(s, f, height, rtl=False):
    """Text as a 1-bit strip of `height` rows, as wide as the ink (so pieces can
    be placed edge to edge). Text is vertically centered in the cell."""
    probe = Image.new("1", (400, height), 1)
    d = ImageDraw.Draw(probe)
    d.fontmode = "1"
    text(d, (20, height // 2), s, f, "lm", rtl=rtl)
    box = ImageOps.invert(probe.convert("L")).getbbox()
    if box is None:
        return Image.new("1", (1, height), 1)
    return probe.crop((box[0], 0, box[2], height))


def fit_strip(s, size, bold, height, rtl=False, max_w=196):
    """render_strip, shrinking the font a point at a time until it fits the screen."""
    while True:
        img = render_strip(s, font(size, bold), height, rtl=rtl)
        if img.width <= max_w or size <= 8:
            return img
        size -= 1


def clamp_w(img):
    if img.width > 200:
        raise SystemExit(f"strip too wide ({img.width}px)")
    return img


# ------------------------------------------------------------------ photos

def find_photo(sci, image_url, photos_dir, download, cache):
    s = slug(sci)
    for d in (ROOT / "overrides", photos_dir):
        if d:
            for ext in ("jpg", "jpeg", "png", "webp"):
                p = Path(d) / f"{s}.{ext}"
                if p.exists():
                    return Image.open(p), str(p)
    cached = cache / f"{s}.img"
    if cached.exists():
        return Image.open(cached), "cache"
    if download and image_url:
        try:
            req = urllib.request.Request(image_url, headers={"User-Agent": UA})
            data = urllib.request.urlopen(req, timeout=30).read()
            img = Image.open(io.BytesIO(data))
            img.load()
            cache.mkdir(parents=True, exist_ok=True)
            cached.write_bytes(data)
            return img, "downloaded"
        except Exception as e:  # network, 4xx, not an image: report and carry on
            return None, f"download failed: {e}"
    return None, "no photo"


# -------------------------------------------------------------------- cards

def get_cutout(photo_img, s, cache, recut, src=None):
    """The bird cut out of its photo (cached as cache/cut/<slug>.png), or None.
    The cache is redone when the photo file (`src`) is newer than the cut-out."""
    path = cache / "cut" / f"{s}.png"
    if src and Path(src).exists() and path.exists() and Path(src).stat().st_mtime > path.stat().st_mtime:
        recut = True
    if path.exists() and not recut:
        return Image.open(path).convert("RGBA")
    rgba = cut_out(photo_img)
    if rgba is None:
        path.with_suffix(".failed").parent.mkdir(parents=True, exist_ok=True)
        path.with_suffix(".failed").write_bytes(b"")
        return None
    path.parent.mkdir(parents=True, exist_ok=True)
    rgba.save(path)
    return rgba


def build_card(sp, photo_img, focus, cutout=None):
    if photo_img is None:
        photo = Image.new("1", PHOTO, 1)
    elif cutout is not None:
        photo = to_1bit(to_gray_cutout(cutout, PHOTO))
    else:
        photo = to_1bit(to_gray(photo_img, PHOTO, focus))
    he_big = clamp_w(fit_strip(sp["he"], 22, True, HE_BIG_H, rtl=True))
    en = clamp_w(fit_strip(sp["en"], 12, False, EN_H))
    he_small = clamp_w(fit_strip(sp["he"], 13, False, HE_SMALL_H, rtl=True, max_w=130))
    out = bytearray(b"BKC1")
    out += bytes([he_big.width, en.width, he_small.width, 0])
    for part in (photo, he_big, en, he_small):
        out += pack_bits(part)
    return bytes(out), photo


def collect_species(data_path, expert_path):
    sp = {}
    for b in json.load(open(expert_path, encoding="utf-8")):
        sp[b["sci"]] = {"sci": b["sci"], "en": b["com"], "he": b["he"], "image": None}
    for b in json.load(open(data_path, encoding="utf-8")):  # data.json wins: it has the photo URL
        sp[b["sci"]] = {"sci": b["sci"], "en": b["name"], "he": b["he_name"], "image": b.get("image")}
    return sorted(sp.values(), key=lambda s: s["sci"])


def contact_sheet(items, path):
    cols, scale, gap = 5, 1, 6
    cw, ch = PHOTO[0] * scale, PHOTO[1] * scale + 14
    rows = (len(items) + cols - 1) // cols
    sheet = Image.new("L", (cols * (cw + gap), rows * (ch + gap)), 255)
    d = ImageDraw.Draw(sheet)
    f = font(10)
    for i, (sci, photo, note) in enumerate(items):
        x, y = (i % cols) * (cw + gap), (i // cols) * (ch + gap)
        sheet.paste(photo.convert("L"), (x, y))
        d.text((x, y + PHOTO[1] + 1), sci + ("  [" + note + "]" if note else ""), font=f, fill=0)
        d.rectangle((x, y, x + cw - 1, y + PHOTO[1] - 1), outline=160)
    sheet.save(path)


# ---------------------------------------------------------------- ui assets

CHARSET = "0123456789:%-o"  # 'o' draws the degree sign
DIGIT_FONTS = [  # name, size, bold, cell height
    ("BAR", 15, True, 18),    # temperature and humidity in the top bar
    ("CLOCK", 17, True, 20),  # HH:MM in the top bar
    ("WEEK", 13, True, 16),   # "heard N times this week"
    ("AGO", 12, True, 14),    # "N min ago"
    ("LIST", 12, False, 14),  # times in the latest-10 list
]
# (name, text, size, bold, height). Digits are drawn live between the pieces.
PHRASES = [
    ("TITLE_LIST", "10 הציפורים האחרונות", 13, True, 16),
    ("HEARD_PRE", "נשמע", 13, True, 16),
    ("HEARD_POST", "פעמים השבוע", 13, True, 16),
    ("HEARD_ONCE", "נשמע פעם אחת השבוע", 13, True, 16),
    ("HEARD_TWICE", "נשמע פעמיים השבוע", 13, True, 16),
    ("HEARD_NONE", "לא נשמע השבוע", 13, True, 16),
    ("AGO_PRE", "לפני", 12, True, 14),
    ("AGO_MIN", "דק׳", 12, True, 14),
    ("AGO_HOURS", "שעות", 12, True, 14),
    ("AGO_HOUR", "לפני שעה", 12, True, 14),
    ("AGO_DAY", "אתמול", 12, True, 14),
]
ICON_ORDER = ["Migrant", "Resident", "Raptor", "Nocturnal", "Waterbird", "Rare",
              "Dawn Caller", "Low confidence", "thermo", "drop"]


def ident(name):
    return re.sub(r"\W+", "_", name).upper()


def c_bytes(name, data):
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    return f"static const uint8_t {name}[] PROGMEM = {{\n  " + ",\n  ".join(rows) + "\n};\n"


def ui_assets_header():
    out = ["// GENERATED by tools/build_pack.py: do not edit. Bitmaps are 1 bit per pixel,",
           "// rows padded to bytes, MSB first, bit 1 = black (Adafruit_GFX drawBitmap).",
           "#pragma once", "#include <stdint.h>", "#include <string.h>", "#ifndef PROGMEM", "#define PROGMEM", "#endif", "",
           "struct Strip { uint8_t w, h; const uint8_t* bits; };",
           "struct Glyph { uint8_t w; const uint8_t* bits; };",
           f"struct DigitFont {{ uint8_t h; Glyph g[{len(CHARSET)}]; }};",
           f'static const char kDigitCharset[] = "{CHARSET}";  // glyph order; \'o\' = degree sign', ""]
    # phrases
    for name, s, size, bold, h in PHRASES:
        img = clamp_w(render_strip(s, font(size, bold), h, rtl=True))
        out.append(c_bytes(f"b_{name}", pack_bits(img)))
    out.append("namespace ui {")
    for name, s, size, bold, h in PHRASES:
        img = clamp_w(render_strip(s, font(size, bold), h, rtl=True))
        out.append(f"static const Strip {name} = {{{img.width}, {h}, b_{name}}};")
    out.append("}  // namespace ui\n")
    # digit fonts
    for fname, size, bold, h in DIGIT_FONTS:
        f = font(size, bold)
        cells = []
        for i, ch in enumerate(CHARSET):
            glyph = "°" if ch == "o" else ch
            adv = max(1, round(f.getlength(glyph)))
            img = Image.new("1", (adv, h), 1)
            d = ImageDraw.Draw(img)
            d.fontmode = "1"
            d.text((0, h // 2), glyph, font=f, fill=0, anchor="lm")
            out.append(c_bytes(f"g_{fname}_{i}", pack_bits(img)))
            cells.append(f"{{{adv}, g_{fname}_{i}}}")
        out.append(f"namespace ui {{ static const DigitFont {fname} = {{{h}, {{{', '.join(cells)}}}}}; }}\n")
    # icons: 12x12, 2 bytes per row
    for name in ICON_ORDER:
        img = Image.new("1", (12, 12), 1)
        for j, row in enumerate(ICONS[name]):
            for i, c in enumerate(row[:12]):
                if c == "#":
                    img.putpixel((i, j), 0)
        out.append(c_bytes(f"i_{ident(name)}", pack_bits(img)))
    out.append("namespace ui {")
    for name in ICON_ORDER:
        out.append(f"static const uint8_t* const ICON_{ident(name)} = i_{ident(name)};")
    out.append("""
// Badge name from data.json ("Migrant", ...) -> 12x12 icon, or nullptr.
static inline const uint8_t* iconForBadge(const char* en) {
  static const char* const names[] = {""" + ", ".join(f'"{n}"' for n in ICON_ORDER[:8]) + """};
  static const uint8_t* const icons[] = {""" + ", ".join("ICON_" + ident(n) for n in ICON_ORDER[:8]) + """};
  for (int i = 0; i < 8; i++) if (strcmp(en, names[i]) == 0) return icons[i];
  return nullptr;
}
}  // namespace ui
""")
    return "\n".join(out)


# --------------------------------------------------------------------- main

def main():
    data = site_json("data.json", arg("--data"), ROOT / "cache")
    expert = site_json("expert-data.json", arg("--expert"), ROOT / "cache")
    photos_dir = arg("--photos")
    download = "--no-download" not in sys.argv
    out = Path(arg("--out", ROOT / "out"))
    fw = Path(arg("--firmware", ROOT / "firmware"))
    cards = fw / "data" / "cards"
    cards.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    crops = json.load(open(ROOT / "crops.json")) if (ROOT / "crops.json").exists() else {}

    if "--no-assets" not in sys.argv:
        (fw / "src").mkdir(parents=True, exist_ok=True)
        (fw / "src" / "ui_assets.h").write_text(ui_assets_header(), encoding="utf-8")
    use_cut = "--no-cutout" not in sys.argv
    recut = "--recut" in sys.argv
    cache = ROOT / "cache"

    species = collect_species(data, expert)
    sheet, report, total = [], [], 0
    for i, sp in enumerate(species, 1):
        img, src = find_photo(sp["sci"], sp["image"], photos_dir, download, ROOT / "cache")
        focus = tuple(crops.get(sp["sci"], (0.5, 0.45)))
        cutout = None
        if img is not None and use_cut and (recut or not (cache / "cut" / f"{slug(sp['sci'])}.failed").exists()):
            cutout = get_cutout(img, slug(sp["sci"]), cache, recut, src)
        blob, panel = build_card(sp, img, focus, cutout)
        (cards / f"{slug(sp['sci'])}.bin").write_bytes(blob)
        total += len(blob)
        ok = img is not None
        sheet.append((sp["sci"], panel, "" if ok else "NO PHOTO"))
        report.append(f"{'ok ' if ok else 'MISSING'}  {sp['sci']:<32} {len(blob):5d} B  {src}")
        print(f"[{i}/{len(species)}] {sp['sci']}: {src}", file=sys.stderr)
    contact_sheet(sheet, out / "contact.png")
    missing = sum(1 for r in report if r.startswith("MISSING"))
    summary = f"{len(species)} cards, {total / 1024:.0f} KB, {missing} without a photo"
    (out / "report.txt").write_text(summary + "\n" + "\n".join(report) + "\n", encoding="utf-8")
    print(summary)


if __name__ == "__main__":
    main()

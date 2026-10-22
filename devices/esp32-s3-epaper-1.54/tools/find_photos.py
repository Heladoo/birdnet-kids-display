"""Find better photos for weak species on Wikimedia Commons and show before / after.

Usage: python3 find_photos.py search "Falco tinnunculus" "Milvus migrans" ...   (or --weak: cache/weak_list.json)
       python3 find_photos.py sheet [--cols 3] [--min-gain 1.25]

search: for each species, queries Commons (up to 20 photos), downloads 600 px thumbnails into
        cache/cand/<slug>/, cuts the bird out of each (U2-Net, like the pack builder) and scores it:
        a bird that is wide in the 200x108 panel box, in one piece, and not cut by the frame scores
        high. Results go to cache/cand/<slug>/candidates.json (best first).
sheet:  renders out/photo_sheet_N.png: for every species, the current card ("before") next to its
        two best candidates ("after"), as the panel would show them. Nothing is written to the pack
        or to overrides/: adopting a photo is a separate step (copy it to overrides/<slug>.jpg).
"""
import io
import json
import re
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

from epaper_image import cut_out, to_1bit, to_gray_cutout

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
CAND = ROOT / "cache" / "cand"
UA = "BirdnetKidsDisplay/1.0 (https://github.com/Heladoo/birdnet-kids-display)"
API = "https://commons.wikimedia.org/w/api.php"
BOX = (200, 108)


def slug(sci):
    return re.sub(r"[^a-z0-9]+", "_", sci.lower()).strip("_")


def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": UA}), timeout=40).read()


def commons_search(sci, limit=20):
    q = {
        "action": "query", "format": "json", "generator": "search", "gsrsearch": f"{sci} filetype:bitmap",
        "gsrnamespace": 6, "gsrlimit": limit, "prop": "imageinfo", "iiprop": "url|size|extmetadata|mime",
        "iiurlwidth": 600,
    }
    data = json.loads(get(API + "?" + urllib.parse.urlencode(q)))
    pages = sorted(data.get("query", {}).get("pages", {}).values(), key=lambda p: p["index"])
    out = []
    for p in pages:
        ii = p["imageinfo"][0]
        if not ii.get("mime", "").startswith("image/jpeg") and not ii.get("mime", "").startswith("image/png"):
            continue
        meta = ii.get("extmetadata", {})
        out.append({
            "title": p["title"], "thumb": ii["thumburl"], "page": ii["descriptionurl"],
            "license": meta.get("LicenseShortName", {}).get("value", "?"),
            "artist": re.sub(r"<[^>]+>", "", meta.get("Artist", {}).get("value", "?"))[:80],
        })
    return out


def box_scale(w, h):
    """Scale that fits a bird of w x h into the panel box with the pipeline's 6 % padding."""
    return min(BOX[0] / (w * 1.12), BOX[1] / (h * 1.12))


def measure(rgba):
    """(score, details) of a cut-out for e-paper: big, whole, in one piece."""
    a = np.asarray(rgba.split()[3]) > 128
    ys, xs = np.where(a)
    if len(xs) == 0:
        return 0.0, {}
    H, W = a.shape
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    w, h = x1 - x0 + 1, y1 - y0 + 1
    s = box_scale(w, h)
    lab, n = ndimage.label(a)
    sizes = np.bincount(lab.ravel())[1:]
    main = float(sizes.max() / sizes.sum())
    touch = int(x0 <= 1) + int(x1 >= W - 2) + int(y0 <= 1) + int(y1 >= H - 2)
    fg = float(a.mean())
    score = min(w * s, 180.0) * main
    if touch >= 2:
        score *= 0.65  # the frame cuts the bird
    if fg < 0.04 or fg > 0.75:
        score *= 0.6
    return score, {"bw": int(w * s), "main": round(main, 3), "touch": touch, "fg": round(fg, 3)}


def search(species, per=20):
    CAND.mkdir(parents=True, exist_ok=True)
    for i, sci in enumerate(species, 1):
        d = CAND / slug(sci)
        d.mkdir(exist_ok=True)
        try:
            found = commons_search(sci, per)
        except Exception as e:
            print(f"[{i}/{len(species)}] {sci}: search failed: {e}", flush=True)
            continue
        scored = []
        for k, c in enumerate(found):
            try:
                img = Image.open(io.BytesIO(get(c["thumb"]))).convert("RGB")
                rgba = cut_out(img)
                if rgba is None:
                    continue
                sc, det = measure(rgba)
                f = d / f"c{k}.jpg"
                img.save(f, quality=90)
                scored.append({**c, "file": f.name, "score": round(sc, 1), **det})
            except Exception as e:
                print(f"   skip {c['title'][:50]}: {e}", flush=True)
            time.sleep(0.15)
        scored.sort(key=lambda c: -c["score"])
        (d / "candidates.json").write_text(json.dumps(scored, ensure_ascii=False, indent=1), encoding="utf-8")
        best = scored[0] if scored else None
        print(f"[{i}/{len(species)}] {sci}: {len(found)} found, {len(scored)} usable, best {best['score'] if best else '-'} "
              f"({best['bw'] if best else '-'} px)", flush=True)


def card_before(sci):
    """The current card's photo: the cached cut-out through the same steps as the pack builder."""
    p = ROOT / "cache" / "cut" / f"{slug(sci)}.png"
    if not p.exists():
        return Image.new("1", BOX, 1), None
    rgba = Image.open(p).convert("RGBA")
    return to_1bit(to_gray_cutout(rgba, BOX)), measure(rgba)[1]


def after(sci, c):
    img = Image.open(CAND / slug(sci) / c["file"]).convert("RGB")
    rgba = cut_out(img)
    return to_1bit(to_gray_cutout(rgba, BOX))


def sheets(cols=3, min_gain=1.25, per_sheet=5):
    names = {}
    for f in ("data.json", "expert-data.json"):
        p = ROOT / "cache" / f
        if p.exists():
            for b in json.load(open(p, encoding="utf-8")):
                names[b["sci"]] = b.get("name") or b.get("com") or b["sci"]
    rows = []
    for d in sorted(CAND.glob("*/candidates.json")):
        sci = next((k for k in names if slug(k) == d.parent.name), d.parent.name.replace("_", " ").capitalize())
        cands = json.loads(d.read_text(encoding="utf-8"))
        before, bdet = card_before(sci)
        bw = bdet["bw"] if bdet else 0
        if not cands:
            continue
        best = cands[0]
        gain = best["bw"] / max(bw, 1) if bdet else 9
        better = gain >= min_gain or (bdet and bdet["main"] < 0.9 <= best["main"])
        rows.append((sci, names.get(sci, sci), before, bdet, cands[:2], better))
    shown = [r for r in rows if r[5]]
    skipped = [r[0] for r in rows if not r[5]]
    font = ImageFont.load_default(15)
    small = ImageFont.load_default(12)
    cw, ch = BOX[0] * 2, BOX[1] * 2
    out_dir = ROOT / "out"
    out_dir.mkdir(exist_ok=True)
    n = 0
    for s in range(0, len(shown), per_sheet):
        chunk = shown[s:s + per_sheet]
        sheet = Image.new("RGB", (cols * (cw + 10) + 10, len(chunk) * (ch + 46) + 10), "white")
        dr = ImageDraw.Draw(sheet)
        for r, (sci, name, before, bdet, cands, _) in enumerate(chunk):
            y = 10 + r * (ch + 46)
            dr.text((10, y), f"{name}  ({sci})", font=font, fill="black")
            tiles = [("BEFORE" + (f"  bird {bdet['bw']} px wide" if bdet else "  (no photo)"), before)]
            for k, c in enumerate(cands):
                tiles.append((f"AFTER {k + 1}  bird {c['bw']} px wide  {c['license']}", after(sci, c)))
            for k, (label, im) in enumerate(tiles[:cols]):
                x = 10 + k * (cw + 10)
                sheet.paste(im.convert("RGB").resize((cw, ch), Image.NEAREST), (x, y + 20))
                dr.rectangle((x, y + 20, x + cw - 1, y + 20 + ch - 1), outline=(160, 160, 160))
                dr.text((x, y + 22 + ch), label, font=small, fill="black")
        n += 1
        sheet.save(out_dir / f"photo_sheet_{n}.png")
    print(f"{len(shown)} species with a clearly better candidate -> {n} sheet(s) in out/; no improvement found for: {', '.join(skipped) or '-'}")


def curated(picks_path, per_image=8):
    """Before / after sheets of hand-picked candidates: picks = {"Falco tinnunculus": 1, ...} where
    the number is the AFTER column (1 = best scored, 2 = second) of `sheet` for that species."""
    picks = json.load(open(picks_path, encoding="utf-8"))
    names = {}
    for f in ("data.json", "expert-data.json"):
        p = ROOT / "cache" / f
        if p.exists():
            for b in json.load(open(p, encoding="utf-8")):
                names[b["sci"]] = b.get("name") or b.get("com") or b["sci"]
    font, small, big = ImageFont.load_default(15), ImageFont.load_default(12), ImageFont.load_default(18)
    cw, ch = BOX[0] * 2, BOX[1] * 2
    cell_w = 2 * cw + 30
    out_dir = ROOT / "out"
    out_dir.mkdir(exist_ok=True)
    items = list(picks.items())
    for n, s in enumerate(range(0, len(items), per_image), 1):
        chunk = items[s:s + per_image]
        rows = (len(chunk) + 1) // 2
        sheet = Image.new("RGB", (2 * cell_w + 10, rows * (ch + 56) + 10), "white")
        dr = ImageDraw.Draw(sheet)
        for i, (sci, k) in enumerate(chunk):
            c = json.loads((CAND / slug(sci) / "candidates.json").read_text(encoding="utf-8"))[k - 1]
            before, bdet = card_before(sci)
            x0, y0 = 10 + (i % 2) * cell_w, 10 + (i // 2) * (ch + 56)
            num = s + i + 1  # the number to answer with ("7: replace")
            dr.rectangle((x0, y0 - 2, x0 + 40, y0 + 18), fill="black")
            dr.text((x0 + 6, y0), str(num), font=big, fill="white")
            dr.text((x0 + 50, y0), f"{names.get(sci, sci)}  ({sci})", font=font, fill="black")
            for j, (im, label) in enumerate((
                    (before, "BEFORE" + (f"  bird {bdet['bw']} px wide" if bdet else "  (no photo)")),
                    (after(sci, c), f"AFTER  bird {c['bw']} px wide  {c['license']}"))):
                x = x0 + j * (cw + 10)
                sheet.paste(im.convert("RGB").resize((cw, ch), Image.NEAREST), (x, y0 + 20))
                dr.rectangle((x, y0 + 20, x + cw - 1, y0 + 20 + ch - 1), outline=(160, 160, 160))
                dr.text((x, y0 + 22 + ch), label, font=small, fill="black")
        sheet.save(out_dir / f"photo_before_after_{n}.png")
    print(f"{len(items)} species -> {(len(items) + per_image - 1) // per_image} sheet(s) out/photo_before_after_N.png")


def main():
    args = sys.argv[1:]
    if not args:
        raise SystemExit(__doc__)
    if args[0] == "search":
        names = args[1:]
        if names == ["--weak"]:
            names = json.load(open(ROOT / "cache" / "weak_list.json", encoding="utf-8"))
        search(names)
    elif args[0] == "sheet":
        cols = int(args[args.index("--cols") + 1]) if "--cols" in args else 3
        gain = float(args[args.index("--min-gain") + 1]) if "--min-gain" in args else 1.25
        sheets(cols, gain)
    elif args[0] == "curated":
        curated(args[1] if len(args) > 1 else ROOT / "photo_picks.json")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()

"""Mockup of a "News" screen for the e-paper: new birds and returning birds, each with a small photo.

Usage: python3 mockup_news.py [out_dir]      (default: ../mockups)

Reads the site's published news.json and data.json (cache/ copies, downloaded if missing), and
writes news-2.png (two items, bigger photos) and news-3.png (three items; the third is an invented
example of a "rare visitor" item that news.json does not produce today), each also as @3x.
The photos are the real cut-outs of the pack builder, dithered the same way.
"""
import json
import sys
import urllib.request
from datetime import datetime
from pathlib import Path

from PIL import Image

from epaper_image import to_1bit, to_gray_cutout
from mockups import W, font, new_canvas, save, text, top_bar

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SITE = "https://raw.githubusercontent.com/Heladoo/Birdnet_kids/main/"

# What the screen says (Hebrew), per kind of news. Day counts come from the item text.
TAG = {"new": "ציפור חדשה!", "back": "חזרה!", "rare": "אורחת נדירה!"}


def slug(sci):
    return sci.lower().replace(" ", "_")


def site_file(name):
    p = ROOT / "cache" / name
    if not p.exists():
        p.parent.mkdir(exist_ok=True)
        p.write_bytes(urllib.request.urlopen(SITE + name, timeout=30).read())
    return json.loads(p.read_text(encoding="utf-8"))


def detail(item):
    """Short Hebrew line under the name."""
    t = item.get("text", "")
    if item["type"] == "new":
        return "נשמעה לראשונה היום"
    if item["type"] == "back":
        digits = "".join(c for c in t if c.isdigit())
        return f"אחרי {digits} ימים" if digits else "אחרי זמן רב"
    return "נדירה באזור"  # rare visitor (an invented kind of item, see the module docstring)


def thumb(sci, size):
    p = ROOT / "cache" / "cut" / f"{slug(sci)}.png"
    return to_1bit(to_gray_cutout(Image.open(p).convert("RGBA"), size))


def news_screen(items, birds, now, photo, rows_h):
    img, d = new_canvas()
    top_bar(img, d, now)
    y = 20
    for i, it in enumerate(items):
        b = birds[it["sci"]]
        ph = thumb(it["sci"], photo)
        img.paste(ph, (W - photo[0] - 3, y + (rows_h - photo[1]) // 2))
        xr = W - photo[0] - 8  # text is right-aligned against the photo (right-to-left layout)
        big = rows_h > 70
        room = xr - 4  # width left of the photo
        he = b.get("he_name") or b["name"]  # a species without a Hebrew name yet shows the English one
        lines = [(TAG[it["type"]], 15 if big else 13, True, True),
                 (he, 17 if big else 14, True, bool(b.get("he_name"))),
                 (detail(it), 13 if big else 11, False, True)]
        for k, (s, size, bold, rtl) in enumerate(lines):
            while size > 8 and d.textlength(s, font=font(size, bold)) > room:  # shrink until it fits
                size -= 1
            text(d, (xr, y + rows_h * (0.22, 0.50, 0.78)[k]), s, font(size, bold), "rm", rtl=rtl)
        y += rows_h
        if i < len(items) - 1:
            d.line((6, y - 1, W - 6, y - 1), fill=0)
    return img


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "mockups"
    out.mkdir(exist_ok=True)
    news = site_file("news.json")
    birds = {b["sci"]: b for b in site_file("data.json")}
    news = [n for n in news if n["sci"] in birds]
    example = {"type": "rare", "sci": "Psittacula eupatria", "text": "Rare visitor"}  # invented, for the layout
    now = datetime.now().replace(second=0, microsecond=0)
    two = (news + [example])[:2]
    three = (news + [example])[:3]
    save(news_screen(two, birds, now, (86, 58), 90), out, "news-2")
    save(news_screen(three, birds, now, (66, 46), 60), out, "news-3")
    print("news items:", [(n["type"], n["sci"]) for n in news], "+ invented example:", example["sci"])


if __name__ == "__main__":
    main()

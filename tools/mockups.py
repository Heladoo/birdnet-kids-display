"""Render the two 200x200 1-bit screens of the BirdNET Kids e-paper display:
A (the bird, with photo) and D (the latest 10), which the second button
switches between.

Usage: python3 mockups.py data.json out_dir --photo bird.jpg [--focus 0.5,0.3]
       [--now 2026-10-18T14:05] [--bird "Red-vented Bulbul" --week 12 --seen 13:58]

Uses the real detections in data.json (newest 10 by last_seen). The photo goes
through epaper_image.py exactly as the real pack builder will. --bird puts that
species first with demo values, so the screen matches a photo you have.
Writes <name>.png at 1x (what the panel shows) and <name>@3x.png for viewing.
"""
import json
import sys
from datetime import datetime

from PIL import Image, ImageDraw, ImageFont, features

from epaper_image import to_1bit, to_gray

W = H = 200
# Any font with Hebrew glyphs: Linux FreeSans, then Windows / macOS Arial.
FONTS = {
    False: ["/usr/share/fonts/truetype/freefont/FreeSans.ttf", "C:/Windows/Fonts/arial.ttf",
            "/System/Library/Fonts/Supplemental/Arial.ttf"],
    True: ["/usr/share/fonts/truetype/freefont/FreeSansBold.ttf", "C:/Windows/Fonts/arialbd.ttf",
           "/System/Library/Fonts/Supplemental/Arial Bold.ttf"],
}
RAQM = features.check("raqm")  # Pillow's right-to-left text support


def font(size, bold=False):
    for path in FONTS[bold]:
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            pass
    raise SystemExit("No Hebrew-capable font found; add one to FONTS in mockups.py")


# ------------------------------------------------------------------- icons

ICONS = {  # 12x12, '#' = black
    "Migrant": [
        "............", ".....##.....", ".....###....", "#....####...",
        "##..#######.", "############", "##..#######.", "#....####...",
        ".....###....", ".....##.....", "............", "............"],
    "Resident": [
        ".....##.....", "....####....", "...######...", "..########..",
        ".##########.", "############", ".##......##.", ".##..##..##.",
        ".##..##..##.", ".##..##..##.", ".##########.", "............"],
    "Raptor": [
        "....####....", "..########..", ".####..####.", "####......##",
        "###.........", "###.........", "####........", ".#####......",
        "..######....", "....####....", "......##....", "............"],
    "Nocturnal": [
        "....####....", "..####......", ".####.......", ".###........",
        "####........", "####........", "####........", "####.......#",
        ".####.....##", ".#########..", "..#######...", "....####...."],
    "Waterbird": [
        "............", "............", "............", ".##...##...##",
        "#..#.#..#.#..", "....#....#...", "............", ".##...##...##",
        "#..#.#..#.#..", "....#....#...", "............", "............"],
    "Rare": [
        "............", "...######...", "..#..##..#..", ".#..#..#..#.",
        "############", ".#...##...#.", "..#..##..#..", "...#.##.#...",
        "....####....", ".....##.....", "............", "............"],
    "Dawn Caller": [
        "............", ".....##.....", ".#...##...#.", "..#......#..",
        "....####....", "...######...", "#.########.#", "..########..",
        "############", "............", "############", "............"],
    "Low confidence": [
        "....####....", "...##..##...", "...#....##..", "........##..",
        ".......##...", "......##....", ".....##.....", ".....##.....",
        "............", ".....##.....", ".....##.....", "............"],
    "thermo": [
        "....##......", "...#..#.....", "...#..#.....", "...#..#.....",
        "...#..#.....", "...#..#.....", "...####.....", "..######....",
        ".########...", ".########...", "..######....", "...####....."],
    "drop": [
        ".....#......", ".....#......", "....###.....", "....###.....",
        "...#####....", "..#######...", "..#######...", ".#########..",
        ".#########..", ".#########..", "..#######...", "....###....."],
}


def icon(img, name, x, y, ink=0):
    for j, row in enumerate(ICONS[name]):
        for i, c in enumerate(row[:12]):
            if c == "#":
                img.putpixel((x + i, y + j), ink)


# ------------------------------------------------------------------ helpers

HE_AGO = {"min": "לפני {} דק׳", "hr1": "לפני שעה", "hr": "לפני {} שעות", "day": "אתמול"}


def ago(now, then):
    m = int((now - then).total_seconds() // 60)
    if m < 60:
        return HE_AGO["min"].format(max(m, 1))
    if m < 120:
        return HE_AGO["hr1"]
    if m < 24 * 60:
        return HE_AGO["hr"].format(m // 60)
    return HE_AGO["day"]


def heard_this_week(n):
    if n == 0:
        return "לא נשמע השבוע"
    if n == 1:
        return "נשמע פעם אחת השבוע"
    if n == 2:
        return "נשמע פעמיים השבוע"
    return f"נשמע {n} פעמים השבוע"


def text(d, xy, s, f, anchor="la", fill=0, rtl=False):
    kw = {}
    if rtl and RAQM:
        kw = {"direction": "rtl", "language": "he"}
    elif rtl:  # Pillow without raqm (common on Windows): reorder with python-bidi
        from bidi.algorithm import get_display
        s = get_display(s)
    d.text(xy, s, font=f, fill=fill, anchor=anchor, **kw)


def top_bar(img, d, now, temp=23.4, hum=61, inverted=True):
    ink, paper = (1, 0) if inverted else (0, 1)
    d.rectangle((0, 0, W - 1, 19), fill=paper)
    if not inverted:
        d.line((0, 19, W, 19), fill=0)
    icon(img, "thermo", 3, 4, ink)
    text(d, (16, 10), f"{temp:.0f}°", font(15, True), "lm", ink)
    icon(img, "drop", 52, 4, ink)
    text(d, (65, 10), f"{hum}%", font(15, True), "lm", ink)
    text(d, (W - 4, 10), now.strftime("%H:%M"), font(17, True), "rm", ink)


def new_canvas():
    img = Image.new("1", (W, H), 1)
    d = ImageDraw.Draw(img)
    d.fontmode = "1"  # no anti-aliasing: e-paper has no grey
    return img, d


# ------------------------------------------------------------------ layouts

PHOTO = (W, 108)  # the photo box of screen A, 2,700 bytes at 1 bit per pixel


def layout_big_photo(bird, now, photo):
    img, d = new_canvas()
    img.paste(photo, (0, 20))
    top_bar(img, d, now)
    text(d, (W // 2, 141), bird["he_name"], font(22, True), "mm", rtl=True)
    text(d, (W // 2, 159), bird["name"], font(12), "mm")
    text(d, (W // 2, 174), heard_this_week(bird["week_count"]), font(13, True), "mm", rtl=True)
    d.line((8, 183, W - 8, 183), fill=0)
    x = 6
    for b in bird["badges"][:3]:
        icon(img, b["en"], x, 187)
        x += 15
    text(d, (W - 6, 193), ago(now, datetime.fromisoformat(bird["last_seen"])), font(12, True), "rm", rtl=True)
    return img


def layout_list(birds, now, sel):
    img, d = new_canvas()
    top_bar(img, d, now)
    text(d, (W // 2, 31), "10 הציפורים האחרונות", font(13, True), "mm", rtl=True)
    y = 41
    for k, b in enumerate(birds[:10]):
        f = font(13, k == sel)
        if k == sel:
            d.rectangle((0, y, W - 1, y + 15), fill=0)
        ink = 1 if k == sel else 0
        text(d, (W - 4, y + 8), b["he_name"], f, "rm", ink, rtl=True)
        text(d, (4, y + 8), datetime.fromisoformat(b["last_seen"]).strftime("%H:%M"), font(12), "lm", ink)
        y += 16
    return img


def save(img, out, name):
    img.save(f"{out}/{name}.png")
    img.convert("L").resize((W * 3, H * 3), Image.NEAREST).save(f"{out}/{name}@3x.png")


def arg(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def main():
    data_path, out = sys.argv[1], sys.argv[2]
    now = datetime.fromisoformat(arg("--now")) if arg("--now") else datetime.now()
    data = json.load(open(data_path, encoding="utf-8"))
    birds = sorted(data, key=lambda b: b["last_seen"], reverse=True)
    if arg("--bird"):
        demo = dict(next(b for b in data if b["name"] == arg("--bird")))
        hh, mm = map(int, arg("--seen", now.strftime("%H:%M")).split(":"))
        demo["last_seen"] = now.replace(hour=hh, minute=mm, second=0).isoformat()
        demo["week_count"] = int(arg("--week", demo["week_count"]))
        birds = [demo] + [b for b in birds if b["name"] != demo["name"]]
    birds = birds[:10]
    focus = tuple(float(v) for v in arg("--focus", "0.5,0.45").split(","))
    photo = to_1bit(to_gray(Image.open(arg("--photo")), PHOTO, focus))
    save(layout_big_photo(birds[0], now, photo), out, "a-bird")
    save(layout_list(birds, now, 0), out, "d-top10-list")


if __name__ == "__main__":
    main()

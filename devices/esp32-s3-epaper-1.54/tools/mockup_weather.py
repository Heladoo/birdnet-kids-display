"""Mockup of a "Today" screen for the e-paper: weather picture, today's expected temperature graph, the
date with the weekday in Hebrew, and a box left free for daily notes.

Usage: python3 mockup_weather.py [out_dir]      (default: ../mockups)

Writes weather.png (partly cloudy day, empty notes box), weather-notes.png (the same with example notes),
weather-rain.png (a rainy day) and weather-icons.png (the icon set), each also as @3x. THE WEATHER DATA IS
SAMPLE DATA: a real screen would take an hourly forecast (temperature, rain chance, weather code) from a
free weather service such as Open-Meteo, fetched with the hourly data fetch, for the station's rounded
coordinates. The clock in the top bar is set to a sample time so that the "now" marker is mid-graph.
"""
import sys
from datetime import datetime
from pathlib import Path

from PIL import Image, ImageDraw

from mockups import W, font, new_canvas, save, text, top_bar

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

WEEKDAY = ["יום ב׳", "יום ג׳", "יום ד׳", "יום ה׳", "יום ו׳", "יום ש׳", "יום א׳"]  # Monday .. Sunday
MONTH = ["ינואר", "פברואר", "מרץ", "אפריל", "מאי", "יוני", "יולי", "אוגוסט", "ספטמבר", "אוקטובר", "נובמבר", "דצמבר"]
CONDITION = {  # Hebrew label per picture
    "sunny": "שמשי", "partly": "מעונן חלקית", "cloudy": "מעונן", "rain": "גשם", "storm": "סופת רעמים",
    "fog": "אובך", "wind": "רוחות", "night": "בהיר",
}


# ------------------------------------------------------------------ icons (drawn big, then thresholded)

S = 4  # drawing scale: 44 px icons are drawn at 176 px and reduced


def _blob(d, shapes, grow, fill):
    for kind, *a in shapes:
        if kind == "c":  # circle: cx, cy, r
            cx, cy, r = a
            d.ellipse((cx - r - grow, cy - r - grow, cx + r + grow, cy + r + grow), fill=fill)
        else:  # rectangle: x0, y0, x1, y1
            x0, y0, x1, y1 = a
            d.rectangle((x0 - grow, y0 - grow, x1 + grow, y1 + grow), fill=fill)


def _cloud(d, dx, dy, k=1.0, fillink=255):
    """An outlined cloud (white inside, black outline), shifted by dx, dy, scaled by k (units of 100)."""
    sh = [("c", 36, 62, 18), ("c", 56, 50, 24), ("c", 78, 62, 16), ("r", 36, 62, 78, 80)]
    sh = [(s[0], *[(v * k) for v in s[1:]]) for s in sh]
    sh = [(s[0], *[(v + (dx if i % 2 == 0 else dy)) for i, v in enumerate(s[1:])]) for s in sh]
    sh = [(s[0], *[v * S for v in s[1:]]) for s in sh]
    _blob(d, sh, 3 * S, 0)  # outline
    _blob(d, sh, 0, fillink)


def _sun(d, cx, cy, r, rays=8, ray_len=11, width=4):
    import math
    cx, cy, r, ray_len, width = cx * S, cy * S, r * S, ray_len * S, width * S
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=0)
    for i in range(rays):
        a = 2 * math.pi * i / rays
        x0, y0 = cx + math.cos(a) * (r + 3 * S), cy + math.sin(a) * (r + 3 * S)
        x1, y1 = cx + math.cos(a) * (r + 3 * S + ray_len), cy + math.sin(a) * (r + 3 * S + ray_len)
        d.line((x0, y0, x1, y1), fill=0, width=width)


def icon(kind, size=44):
    """A 1-bit weather picture of `size` px (sunny, partly, cloudy, rain, storm, fog, wind, night)."""
    big = Image.new("L", (100 * S, 100 * S), 255)
    d = ImageDraw.Draw(big)
    if kind == "sunny":
        _sun(d, 50, 50, 17, ray_len=13, width=5)
    elif kind == "partly":
        _sun(d, 36, 36, 13, ray_len=9, width=4)
        _cloud(d, 4, 8)
    elif kind == "cloudy":
        _cloud(d, -3, -2, 1.1)
    elif kind in ("rain", "storm"):
        _cloud(d, -2, -12)
        if kind == "rain":
            for x in (36, 54, 72):
                d.line((x * S, 76 * S, (x - 8) * S, 98 * S), fill=0, width=7 * S)
        else:
            bolt = [(56, 62), (36, 86), (50, 86), (42, 104), (72, 76), (58, 76), (68, 62)]
            d.polygon([(x * S, y * S) for x, y in bolt], fill=0)
    elif kind == "fog":
        for i, y in enumerate((30, 48, 66, 84)):
            x0, x1 = (18, 78) if i % 2 == 0 else (28, 88)
            d.line((x0 * S, y * S, x1 * S, y * S), fill=0, width=6 * S)
    elif kind == "wind":
        for y, x1 in ((34, 70), (52, 84), (70, 62)):
            d.line((12 * S, y * S, x1 * S, y * S), fill=0, width=6 * S)
        d.arc(((70 - 9) * S, (34 - 18) * S, (70 + 9) * S, 34 * S), 90, 360, fill=0, width=6 * S)   # curl on the top line
        d.arc(((62 - 9) * S, 70 * S, (62 + 9) * S, (70 + 18) * S), 270, 450 - 360 + 360, fill=0, width=6 * S)  # and the bottom one
    elif kind == "night":
        d.ellipse((24 * S, 22 * S, 76 * S, 74 * S), fill=0)
        d.ellipse((40 * S, 14 * S, 92 * S, 66 * S), fill=255)
        for x, y in ((70, 66), (84, 40), (58, 84)):
            d.line(((x - 6) * S, y * S, (x + 6) * S, y * S), fill=0, width=3 * S)
            d.line((x * S, (y - 6) * S, x * S, (y + 6) * S), fill=0, width=3 * S)
    small = big.resize((size, size), Image.LANCZOS)
    return small.point(lambda v: 0 if v < 150 else 255).convert("1")


# ------------------------------------------------------------------ the screen

def graph(img, d, temps, rain, now_h, box):
    """Temperature curve for hours 0..24 with rain-chance bars, the min and max, and a 'now' marker."""
    x0, y0, x1, y1 = box
    lo, hi = min(temps) - 2, max(temps) + 2
    top = y0 + 12  # room for the max label
    bot = y1 - 12  # room for the hour labels and rain bars

    def X(h):
        return x0 + (x1 - x0) * h / 24.0

    def Y(t):
        return bot - (bot - top) * (t - lo) / (hi - lo)

    d.line((x0, y1 - 10, x1, y1 - 10), fill=0)  # baseline
    for h in (0, 6, 12, 18, 24):  # ticks and labels (the end ones aligned inward so they are not cut off)
        d.line((X(h), y1 - 10, X(h), y1 - 7), fill=0)
        text(d, (X(h), y1 - 1), str(h), font(10), {0: "lb", 24: "rb"}.get(h, "mb"))
    for i, p in enumerate(rain):  # rain chance bars (0..100 %), 8 px at 100 %
        hgt = round(12 * p / 100)
        if hgt:
            d.rectangle((X(i) + 1, y1 - 10 - hgt, X(i + 1) - 1, y1 - 11), fill=0)
    pts = [(X(i + 0.5), Y(t)) for i, t in enumerate(temps)]
    for x, y in pts:  # light shading under the curve (a quarter of the dots)
        for yy in range(int(y) + 2, int(bot) + 1):
            if (int(x) % 2 == 0) and (yy % 2 == 0):
                d.point((int(x), yy), fill=0)
    for (xa, ya), (xb, yb) in zip(pts, pts[1:]):
        for xx in range(int(xa), int(xb) + 1):  # fill the columns between the points
            t = (xx - xa) / max(xb - xa, 1)
            if (int(xx) % 2 == 0):
                for yy in range(int(ya + (yb - ya) * t) + 2, int(bot) + 1):
                    if yy % 2 == 0:
                        d.point((xx, yy), fill=0)
    d.line(pts, fill=0, width=2)
    hmax, hmin = temps.index(max(temps)), temps.index(min(temps))
    text(d, (X(hmax + 0.5), Y(max(temps)) - 3), f"{max(temps)}°", font(11, True), "mb")
    text(d, (X(hmin + 0.5), Y(min(temps)) - 3), f"{min(temps)}°", font(11, True), "mb")
    # "now": a dashed vertical line and a dot on the curve
    xn = X(now_h)
    yn = Y(temps[min(int(now_h), 23)])
    for yy in range(top - 2, int(y1 - 10), 4):
        d.line((xn, yy, xn, yy + 1), fill=0)
    d.ellipse((xn - 3, yn - 3, xn + 3, yn + 3), fill=1, outline=0, width=2)


def notes_box(d, box, notes=()):
    """A frame left free for daily notes (dotted, with ruled lines); example notes if given."""
    x0, y0, x1, y1 = box
    for x in range(x0, x1, 4):  # dotted frame
        d.point((x, y0), fill=0)
        d.point((x, y1), fill=0)
    for y in range(y0, y1, 4):
        d.point((x0, y), fill=0)
        d.point((x1, y), fill=0)
    text(d, (x1 - 5, y0 + 9), "הערות", font(11, True), "rm", rtl=True)
    for k, y in enumerate((y0 + 27, y0 + 40, y0 + 53)):
        if k < len(notes):
            text(d, (x1 - 6, y - 7), notes[k], font(12), "rm", rtl=True)
        for x in range(x0 + 8, x1 - 7, 3):  # ruled line
            d.point((x, y), fill=0)


def weather_screen(now, kind, hi_lo, temps, rain, notes=()):
    img, d = new_canvas()
    top_bar(img, d, now)
    img.paste(icon(kind, 44), (4, 23))
    hi, lo = hi_lo
    text(d, (52, 36), f"{hi}° / {lo}°", font(15, True), "lm")
    text(d, (52, 55), CONDITION[kind], font(11), "lm", rtl=True)
    text(d, (W - 5, 36), WEEKDAY[now.weekday()], font(23, True), "rm", rtl=True)
    text(d, (W - 5, 56), f"{now.day} ב{MONTH[now.month - 1]}", font(13), "rm", rtl=True)
    d.line((6, 70, W - 6, 70), fill=0)
    graph(img, d, temps, rain, now.hour + now.minute / 60.0, (10, 74, W - 8, 134))
    notes_box(d, (6, 138, W - 6, 196), notes)
    return img


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "mockups"
    out.mkdir(exist_ok=True)
    today = datetime.now()
    now = today.replace(hour=14, minute=20, second=0, microsecond=0)  # sample time: the marker sits mid-graph
    temps = [19, 18, 18, 17, 17, 17, 18, 19, 21, 23, 25, 26, 27, 27, 27, 26, 25, 23, 22, 21, 20, 20, 19, 19]
    rain = [0] * 18 + [10, 20, 30, 20, 10, 0]
    save(weather_screen(now, "partly", (27, 17), temps, rain), out, "weather")
    save(weather_screen(now, "partly", (27, 17), temps, rain, ("חוג שחייה ב־17:00", "להביא מעיל גשם")), out, "weather-notes")
    rainy = [16, 16, 15, 15, 15, 15, 16, 16, 17, 18, 19, 19, 20, 20, 19, 19, 18, 18, 17, 17, 16, 16, 16, 15]
    wet = [40, 50, 60, 70, 80, 80, 90, 90, 80, 70, 60, 50, 50, 60, 70, 80, 70, 60, 50, 40, 30, 30, 20, 20]
    save(weather_screen(now, "rain", (20, 15), rainy, wet), out, "weather-rain")
    kinds = list(CONDITION)
    sheet = Image.new("L", (4 * 70 + 10, 2 * 76 + 10), 255)
    sd = ImageDraw.Draw(sheet)
    sd.fontmode = "1"
    for i, k in enumerate(kinds):
        x, y = 10 + (i % 4) * 70, 8 + (i // 4) * 76
        sheet.paste(icon(k, 44).convert("L"), (x + 8, y))
        text(sd, (x + 30, y + 54), CONDITION[k], font(11), "mt", rtl=True)
    sheet.convert("1").save(out / "weather-icons.png")
    sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(out / "weather-icons@3x.png")
    print("sample day:", WEEKDAY[now.weekday()], now.day, MONTH[now.month - 1])


if __name__ == "__main__":
    main()

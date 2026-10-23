"""Get a screen from a running device as a PNG (debug builds only).

Usage: python3 dump_screen.py d [out.png]      (d = today's weather screen, a = the bird screen,
                                                f = ask the site for birds and the forecast first)

Needs a firmware built with -DCLIP_LOG (env bird_clips or bird_fast) and the board on USB, awake. The
device draws the real screen into a RAM canvas and sends it over serial; this script saves it as a PNG
(and a 3x copy). Close any serial monitor first.
"""
import sys
import time
from pathlib import Path

import serial
from PIL import Image
from serial.tools import list_ports


def main():
    which = (sys.argv[1] if len(sys.argv) > 1 else "d")[0]
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parent.parent / "out" / f"screen_{which}.png"
    port = next((p.device for p in list_ports.comports() if p.vid == 0x303A), None)
    if not port:
        raise SystemExit("no ESP32-S3 serial port found")
    s = serial.Serial(port, 115200, timeout=0.5)
    if which == "f":
        s.write(b"f")
        time.sleep(25)  # Wi-Fi, birds, forecast
        which = "d"
    s.reset_input_buffer()
    s.write(which.encode())
    rows, started, t0 = [], False, time.time()
    while time.time() - t0 < 20:
        line = s.readline().decode(errors="replace").strip()
        if line == "SCREEN-BEGIN":
            started = True
        elif line == "SCREEN-END":
            break
        elif started and len(line) == 50:
            rows.append(bytes.fromhex(line))
    if len(rows) != 200:
        raise SystemExit(f"got {len(rows)} rows, expected 200")
    img = Image.new("1", (200, 200), 1)
    for y, r in enumerate(rows):
        for x in range(200):
            img.putpixel((x, y), 1 if r[x // 8] & (0x80 >> (x % 8)) else 0)
    out.parent.mkdir(exist_ok=True)
    img.save(out)
    img.convert("L").resize((600, 600), Image.NEAREST).save(out.with_name(out.stem + "@3x.png"))
    print(out)


if __name__ == "__main__":
    main()

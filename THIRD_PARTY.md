# Third-party material and what is not covered by the MIT licence

The [MIT licence](LICENSE) covers the source code written for this repository. Everything below keeps
its own terms. This is a summary to help you comply, not legal advice.

## Libraries the firmware is built with (downloaded at build time, not stored here)

| Library | Licence | Notes |
|---|---|---|
| [GxEPD2](https://github.com/ZinggJM/GxEPD2) (e-paper driver) | GPL-3.0 | **copyleft** |
| [arduino-libhelix](https://github.com/pschatzmann/arduino-libhelix) v0.9.4 (MP3 decoding) | GPL-3.0 wrapper around RealNetworks' Helix decoder (RPSL / RCSL) | **copyleft**; the Helix terms are unusual, read them before distributing binaries |
| [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library), Adafruit BusIO | BSD | |
| [ArduinoJson](https://arduinojson.org) | MIT | |
| [Arduino-ESP32](https://github.com/espressif/arduino-esp32) via [pioarduino](https://github.com/pioarduino/platform-espressif32) | LGPL-2.1 and others (ESP-IDF: Apache-2.0) | |

**What this means for a compiled firmware.** This repository ships source only, under MIT, and none of the
libraries above. A firmware image you build links GxEPD2 and the Helix wrapper (both GPL-3.0). If you
**distribute that binary** (a release file, a web flasher, a device you give away), the GPL-3.0 terms
apply to it: you must offer the corresponding source and the licence. Building and flashing for yourself
needs nothing.

## Code adapted from elsewhere

- `devices/esp32-s3-epaper-1.54/firmware/src/sound/es8311_min.h`: register sequence and clock table
  adapted from Espressif's `esp_codec_dev` (Apache-2.0), as shipped in Waveshare's example. The original
  copyright and SPDX notice are kept in the file header.
- Pin numbers come from Waveshare's published example code and schematic
  ([waveshareteam/ESP32-S3-ePaper-1.54](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54)); no code is
  copied from it.

## Tools (run on your PC, not part of the firmware)

Pillow, NumPy, SciPy (BSD-style), [rembg](https://github.com/danielgatis/rembg) (MIT) with the U2-Net
model (Apache-2.0), python-bidi (LGPL). They are installed with `pip` and not distributed here.

## Photos and the bird pack: **not** MIT

- The bird photos come from Wikimedia Commons under their own licences (mostly CC BY and CC BY-SA).
  The few photos kept in the repository (`devices/*/overrides/`) are credited, one by one with a link,
  licence and author, in `overrides/CREDITS.md`. Their licences stay as listed there.
- The pack builder downloads the site's other photos when you run it. The generated cards (1-bit,
  cut-out versions of those photos) are **adaptations** of CC-licensed works, so they carry the
  photographers' terms (attribution, and share-alike for CC BY-SA). The cards are not committed; if you
  publish a pack, publish the credits with it.

## Weather data

The forecast on the Today screen comes from [Open-Meteo](https://open-meteo.com): data under CC BY 4.0, so
credit "Weather data by Open-Meteo.com". Their free service is meant for non-commercial use; read their
terms if you use it otherwise.

## Data and sounds

The displays read the public files of the BirdNET Kids website (`data.json`, `news.json`, and the
`audio/` clips, which are recordings made by the station owner). See `docs/data-contract.md`.
The Wikipedia/Commons photo credits are carried in the site's `data.json` (`credit`).

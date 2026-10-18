# BirdNET Kids on e-paper: plan

The **Home display**: a small desk display, the Waveshare **ESP32-S3-ePaper-1.54**, that shows one of the 10 most recently heard birds from the BirdNET Kids site. A top row shows temperature, humidity and time, and the screen refreshes every 5 minutes.

The tools in `tools/` (Python + Pillow, run on the PC):
- `epaper_image.py` turns a bird photo into what the panel can show (section 4).
- `photo_steps.py` renders a side-by-side sheet of every step, for reviewing crops.
- `mockups.py` renders screens A and D from the site's real `data.json` and a photo.

## 1. Decisions in one place

| Question | Decision |
|---|---|
| Images | **The real photo, manipulated by code, never redrawn.** Crop it on the bird, convert to grayscale, boost the contrast, fade the blurred background, then dither. The panel only has black and white, so grey becomes dot density. About 2.7 KB per bird. |
| Local server on the PC? | **Not needed.** The ESP reads `data.json` straight from GitHub. The pictures, names and icons are flashed onto the ESP. |
| All species, or only the top 30? | **All of them fit.** 160 species × ~4 KB ≈ 650 KB, out of 8 MB of flash. |
| Screens | **A**: the bird. **D**: the latest 10. The second button switches between them. |
| Refresh | Wake every 5 minutes: read the sensor and clock, fetch the data, redraw the screen that is showing, and sleep. Do a full refresh when the picture changes, a partial refresh otherwise, and one full refresh at least every 30 minutes to clear ghosting. |
| Which bird is on A | Rotate through the latest 10, one step every 5 minutes. A newly arrived detection jumps to the front. |
| Language | Hebrew name large, English name small. Text is rendered on the PC, so the ESP needs no right-to-left text engine. |

## 2. Hardware (to confirm on the device)

Taken from Waveshare's product listings. The plan was written in a cloud session without the board, so **pin numbers must be read from Waveshare's demo code** in phase 0 and entered in `firmware/include/board_pins.h`:

- ESP32-S3 (PICO-1-N8R8: 8 MB flash, 8 MB PSRAM; some listings say N4R2), Wi-Fi and BLE 5.
- 1.54" e-paper, 200×200, black and white (listed as "2 grey levels"), SPI. About 2 s for a full refresh and 0.3 s for a partial one.
- **SHTC3** temperature and humidity sensor and a **PCF85063** real-time clock, both on I²C.
- ES8311 audio codec with mic, plus **speakers (connected, but not used for now)**, a TF (microSD) slot, and Li-ion charging. Keep the codec and amplifier powered down so they draw nothing.
- **Two buttons: BOOT and PWR.** BOOT is usually GPIO0 and can wake the chip from deep sleep. PWR is often tied to a power-latch circuit, with a long press switching the board off.

## 3. Architecture

```
 BirdNET (PC) ──export, hourly──▶ GitHub: Heladoo/Birdnet_kids (data.json)
                                          │  HTTPS every 5 min (ETag: skip if unchanged)
                                          ▼
 tools/build_pack.py (PC, once / when new species appear)    ESP32-S3
   data.json + expert-data.json                     ├─ SHTC3 → °C, %RH
   → download photos → epaper_image.py              ├─ PCF85063 ← NTP (Asia/Jerusalem)
   → render HE/EN names, phrases, badge icons       ├─ LittleFS: /cards/<sci>.bin (~160 × 4 KB)
   → littlefs image ──USB flash──────────────────▶  └─ e-paper 200×200
```

**Data fetch.** GET `https://raw.githubusercontent.com/Heladoo/Birdnet_kids/main/data.json` (~250 KB). Parse it as a stream with an ArduinoJson *filter* that keeps only `sci, name, he_name, last_seen, week_count, badges[].en`, then sort by `last_seen` and keep 10. Send `If-None-Match` so an unchanged file costs a single 304 response.
*Later, optional:* have the export also write a ~1.5 KB `device.json` holding just the top 10.

**Unknown species** (heard after the pack was built): show a generic bird outline with the names in a built-in font (U8g2 has a Hebrew font). Rebuild and reflash the pack whenever convenient.

## 4. The photo pipeline (`epaper_image.py`)

Each step manipulates the site's own photo (the `image` URL in `data.json`):

1. **Crop** to the 200×108 photo box around a focus point (default: centre, slightly high). `crops.json` in the pack builder can override the focus per species.
2. **Grayscale** with autocontrast.
3. **Local contrast** (wide unsharp mask) and a small **sharpen**, so feathers, eye and beak survive the dots.
4. **Lift the mid-tones** (gamma 0.8): dots look darker on paper than the same grey on a screen.
5. **Fade the background.** Bird photos have a sharp bird on a blurred background. Areas with little detail are pushed toward white, so the background becomes quiet, light dots instead of noise.
6. **Dither.** Atkinson is the default: it passes on less error, so whites stay white. Floyd–Steinberg keeps more mid-tone but looks noisier.

Optional: **4 greys.** The panel is sold as black and white. Getting 4 greys out of this controller needs a custom waveform, which isn't confirmed for this panel. Try it in phase 0 as a stretch goal; it would double storage to ~5.4 KB per bird, which still fits.

What the review showed: birds on a soft background (most Wikimedia lead photos) come out well. Birds in front of sharp branches, or backlit, are the hard case. That is what the contact sheet and `overrides/<sci>.jpg` (a hand-picked photo) are for.

`build_pack.py` (phase 1) runs this for every species in `data.json` and `expert-data.json`. It uses the photos already on this PC where they exist, otherwise downloads the `image` URL into `cache/`, renders the Hebrew and English names as 1-bit strips, and writes `/cards/<sci-slug>.bin`: a small header plus raw 1-bit rows, which is what `display.drawBitmap()` takes. It also writes a contact sheet.

## 5. Screens

All screens share the top row: thermometer °C, droplet %RH, and HH:MM on the right.

**A: the bird** (render with `tools/mockups.py --photo …`; not committed, because the sample photo's license is unknown)
- Photo box 200×108.
- Hebrew name (large), English name (small).
- **"נשמע XX פעמים השבוע"** from `week_count`, with the special cases "נשמע פעם אחת השבוע", "נשמע פעמיים השבוע" and "לא נשמע השבוע".
- Badge icons (migrant, resident, raptor, nocturnal, waterbird, rare, dawn caller) and "לפני 7 דק׳".
- On the ESP, the phrases are pre-rendered bitmaps; only the digits are drawn live.

**D: the latest 10** (`mockups/d-top10-list.png`)
- 10 rows with time and Hebrew name, newest first.
- The bird currently shown on A is highlighted.

## 6. Buttons

| Button | Short press | Long press |
|---|---|---|
| **PWR** | **Refresh now**: reconnect, fetch, full refresh (also clears ghosting) | Power off (keep the hardware behaviour) |
| **BOOT** | **Switch screen A ⇄ D**: no Wi-Fi, redraws in about 2 s | (free for now) |

The chosen screen stays until the button is pressed again, and the 5-minute refresh updates whichever screen is showing.

Wake sources while asleep: the timer (5 min) plus BOOT (ext0 on GPIO0). PWR's GPIO is checked in phase 0.

## 7. Firmware outline (PlatformIO, Arduino-ESP32 3.x)

- Libraries: **GxEPD2** (SSD1681 200×200 driver class; confirm against Waveshare's demo), **ArduinoJson 7**, an SHTC3 driver (a few lines over `Wire`), a minimal PCF85063 read/write, LittleFS.
- `main`: wake → read SHTC3 (before Wi-Fi heats the board) → time from RTC → if the timer woke us: Wi-Fi + fetch (ETag), and NTP→RTC once a day → draw A or D → `hibernate()` the panel → deep sleep for the rest of the 5 minutes.
- State kept across deep sleep in RTC memory: current screen (A/D), index into the top 10, last ETag, the top-10 list, and a partial-refresh counter.
- Wi-Fi credentials in a `secrets.h` (git-ignored).
- Debug hooks, as in the games: a serial command and a build flag that draw A and D with fixed data, with `tools/mockups.py` as the reference picture.
- Power: about 4 s awake per 5 minutes. On USB that's nothing; on a small LiPo, expect roughly 1–2 weeks (to be measured).
- Sensor accuracy: the SHTC3 sits next to the ESP. Read it straight after waking, and add a calibration offset if it reads warm.

## 8. Phases

*Status: phase 1's builder (`tools/build_pack.py`) and phase 2's firmware (`firmware/`) are written but untested on hardware; phase 0 is open. See `CLAUDE.md`.*

0. **Bring-up (local, on the PC):** flash Waveshare's demo, note the pins, and check the display, SHTC3, RTC and both buttons. Stretch: try 4-grey mode.
1. **Pack builder:** `build_pack.py` → contact sheet → LittleFS image. Fix bad crops with `crops.json` and overrides.
2. **Firmware v1:** screens A and D, top bar, 5-minute cycle, both buttons, deep sleep.
3. **Extras:** a "new bird!" screen from `news.json` and `device.json` from the export. Sound stays out until decided otherwise.

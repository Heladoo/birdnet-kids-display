# Home display

An e-paper desk display for BirdNET Kids: a Waveshare **ESP32-S3-ePaper-1.54** (200×200, black/white) connected to this PC by USB. It shows one of the 10 most recently heard birds from the BirdNET Kids site's published `data.json` (github.com/Heladoo/Birdnet_kids, a separate repo), plus temperature, humidity and time, and wakes every 5 minutes (the bird changes every 20). The full plan is `PLAN.md`; read it first.

## Agreed with the owner

- **Screens:** A (the bird: photo, Hebrew + English name, "נשמע XX פעמים השבוע", badges, "לפני X דק׳") and D (the latest 10, with the bird on A highlighted). Top row on both: °C, %RH, HH:MM.
- **Buttons:** BOOT short = switch screen A ⇄ D; BOOT held 1.5 s = play the bird's recording (chirp, then the clip; a new press stops it). PWR short = next bird (no Wi-Fi); PWR held 1.5 s = refresh over Wi-Fi. Any press restarts the 20-minute rotation timer.
- **Pictures:** the real bird photo, manipulated by code (crop, grayscale, contrast, background fade, Atkinson dither) by `tools/epaper_image.py`. Never redraw birds. All species go on the ESP; no local server.
- **Speakers:** off except while a clip plays (BOOT held). Code in `firmware/src/sound/`, plan and results in `PLAN.md` section 9 (stages 0-2 done); test envs `sound_beep`, `sound_stream`, and `bird_clips` (the normal firmware plus clip logs on serial). No icon or redraw while sound plays, on purpose; instead the green LED (GPIO3, active low) blinks at 2 Hz from the hold to the end of the clip (own task, `ledBlinkSet` in `main.cpp`).
- **Top bar:** temperature, humidity, then a plug symbol when a cable is in or the battery icon when unplugged, then the clock. The board has no pin for VBUS or charge status (the charger's STAT only drives its LED, see the schematic), so "plugged" = a PC on USB (certain) or the battery voltage at 4.18 V or more (a guess for wall chargers). The sensor read is retried and the last good value kept up to 15 min, so `--` should be rare; `env board_diag` (src/diag) stress-tests the SHTC3 (700 reads, 0 failures) and prints the battery voltage.
- **Data source:** `https://raw.githubusercontent.com/Heladoo/Birdnet_kids/main/data.json`, read by the ESP over Wi-Fi.

## Where things are

- `tools/epaper_image.py`: photo → panel image (`to_gray`, `to_1bit`, `to_4gray`).
- `tools/photo_steps.py out.png photo.jpg@x,y …`: side-by-side sheet of each step, for checking crops.
- `tools/mockups.py data.json out --photo bird.jpg --focus x,y` (data.json: download it from the site): renders screens A and D (`mockups/` holds D).
- `tools/build_pack.py`: the bird-picture pack and `firmware/src/ui_assets.h`; the card format is in its docstring.
- `firmware/`: the ESP32-S3 firmware (PlatformIO), with `test/run.sh` for the PC test.
- Needs Python 3 + Pillow. If Pillow has no raqm (often on Windows), `pip install python-bidi` for Hebrew order.

## Status and next steps

**Working on the real board (2026-10-18):** the firmware compiles and runs, the pack is on the board, and both buttons, the Wi-Fi fetch, the top bar and the 5-minute deep-sleep cycle work (the bird changes every 20 minutes; with a PC on USB it stays awake instead of sleeping). Board: ESP32-S3-PICO-1 (8 MB flash, 8 MB PSRAM), SSD1681 panel (`GxEPD2_154_D67`). The 5-minute timer rotation has not been watched over a long run yet.

Done, with how:
- **Phase 0:** pins taken from Waveshare's demo repo (`waveshareteam/ESP32-S3-ePaper-1.54`, `02_Example/Arduino/*/user_config.h`) and recorded in `firmware/include/board_pins.h`. EPD power (GPIO6) and the audio supply (GPIO42) are **active low**; GPIO17 is the battery power latch; GPIO18 is the PWR button.
- **Phase 1:** `tools/build_pack.py` builds all 160 species. The bird is **cut out of its photo** with U2-Net (`pip install "rembg[cpu]"`, local, ~1 s per photo; model `u2net.onnx`, 176 MB, auto-downloaded to `~/.rembg`), cropped tight, and drawn on white with a thin outline (`epaper_image.to_gray_cutout`). Cut-outs are cached in `cache/cut/` and redone when the photo file is newer. Falls back to the old crop + background fade (`--no-cutout`) if the cut looks wrong. `overrides/` holds hand-picked Commons photos (credits in `overrides/CREDITS.md`): so far the Rose-ringed Parakeet and the Eurasian Scops-Owl. The Bee-eater (`Merops apiaster`) has no photo.
- **Phase 2:** buttons are **BOOT (GPIO0) = switch screen A/D**, **PWR (GPIO18) short = next bird from memory (no Wi-Fi), held 1.5 s = refresh over Wi-Fi**.

How to build and flash (PC-only; nothing runs on the Pi):
- PlatformIO Core 6.1 is too old for pioarduino. Use a venv with a newer core: `python -m venv ~/pio-epaper-venv`, `pip install platformio pillow python-bidi numpy "rembg[cpu]"`, and set `PLATFORMIO_CORE_DIR=~/.platformio-epaper`. **Run `pio` from PowerShell, not Git Bash**: the tool installer refuses MSYS.
- `pio run -e bird_display -t upload`, and `pio run -e bird_display -t uploadfs` for the pack (`firmware/data/cards/`, git-ignored). `-e demo` shows fixed data (serial `a`/`d`/`n`, `p` prints button levels, buttons work). `-e bird_debug` logs wifi/fetch/parse over serial and stays awake.
- **Flashing a sleeping board:** in deep sleep the USB port does not exist, and holding BOOT while plugging in does not help on battery. Start a watcher that waits for a new COM port and runs `esptool write_flash 0x0 .pio/build/<env>/firmware.factory.bin` at once, then press a button; the port is there for a few seconds. The factory flash image is backed up at `../epaper-backup/factory_flash_8MB.bin` (local only).
- `firmware/include/secrets.h` (git-ignored) holds the Wi-Fi name and password (2.4 GHz).

Bug fixed: `deserializeJson` straight from the TLS stream stopped after 3 entries (`IncompleteInput`), because the stream returns -1 between packets. `PatientReader` in `main.cpp` waits for data until the server closes the connection.

Next:
1. Weak photos: Accipiter nisus, Botaurus stellaris, Asio otus, Buteo buteo, Dendrocopos syriacus and others still look poor; find better Commons photos (put them in `overrides/`) and add the Bee-eater.
2. Watch a long run: does the bird rotate every 20 minutes (and restart on a button press), does the clock stay right, how long does a battery last.
3. Species heard after the pack was built show the English name only: rebuild and re-flash the pack now and then.
4. Phase 3 extras: a "new bird!" screen, `device.json` from the export.

## Firmware notes

- `src/logic.h` (newest-10, rotation, "ago" and "heard" phrases) and `src/screens.h` (screens A and D) have no Arduino dependency. `firmware/test/run.sh` builds them with g++, checks them against the real `data.json` (slug rule, newest-10 order, rotation, phrases), and renders both screens to `firmware/test/out/*.png`. `mockups/firmware-vs-mockup.png` shows that render (left) next to `tools/mockups.py` (right), with a synthetic stand-in photo.
- Wake logic (`src/main.cpp`): timer = fetch + next bird; BOOT = switch A/D without Wi-Fi; PWR short = next bird without Wi-Fi, PWR held 1.5 s = refresh now. After a button wake it stays awake 4 s so presses can follow each other.
- Always a full refresh (about 2 s), no partial refresh: a partial refresh after deep sleep depends on the controller's RAM state and is untested on this panel. Revisit after bring-up if the 5-minute cycle should be quieter.
- Species heard after the pack was built show their English name in a built-in font (no pack card, no Hebrew); rebuild and re-flash the pack to include them.
- The TLS certificate of `raw.githubusercontent.com` is not checked (`setInsecure`): the data is public and only drives the display.
- The amplifier pin is driven off and held through sleep, and the codec is never started (the sound POC is a separate env).

### Verified and unverified

Verified on the board: pins, the panel and `GxEPD2_154_D67`, the SHTC3 reading, NTP, the clock, both buttons waking from deep sleep, the Wi-Fi/HTTPS fetch with ArduinoJson's filter (via `PatientReader`), and the LittleFS pack.

Still unverified: the PCF85063 on its own (time is set from NTP; the RTC fallback after a power loss is untested), the `If-None-Match` 304 path of raw.githubusercontent.com, behaviour on battery (power latch, PWR long-press power-off), the Israel time-zone string across a DST change, battery life, and a long unattended run.

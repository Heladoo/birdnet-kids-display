# Home display

An e-paper desk display for BirdNET Kids: a Waveshare **ESP32-S3-ePaper-1.54** (200×200, black/white) connected to this PC by USB. It shows one of the 10 most recently heard birds from this repo's `data.json`, plus temperature, humidity and time, and refreshes every 5 minutes. The full plan is `PLAN.md`; read it first.

## Agreed with the owner

- **Screens:** A (the bird: photo, Hebrew + English name, "נשמע XX פעמים השבוע", badges, "לפני X דק׳") and D (the latest 10, with the bird on A highlighted). Top row on both: °C, %RH, HH:MM.
- **Buttons:** PWR short = refresh now (long = power off, as the hardware does). BOOT short = switch A ⇄ D.
- **Pictures:** the real bird photo, manipulated by code (crop, grayscale, contrast, background fade, Atkinson dither) by `tools/epaper_image.py`. Never redraw birds. All species go on the ESP; no local server.
- **Speakers are connected but must not be used for now.** Keep the codec and amplifier off.
- **Data source:** `https://raw.githubusercontent.com/Heladoo/Birdnet_kids/main/data.json`, read by the ESP over Wi-Fi.

## Where things are

- `tools/epaper_image.py`: photo → panel image (`to_gray`, `to_1bit`, `to_4gray`).
- `tools/photo_steps.py out.png photo.jpg@x,y …`: side-by-side sheet of each step, for checking crops.
- `tools/mockups.py ../data.json out --photo bird.jpg --focus x,y`: renders screens A and D (`mockups/` holds D).
- `tools/build_pack.py`: the bird-picture pack and `firmware/src/ui_assets.h`; the card format is in its docstring.
- `firmware/`: the ESP32-S3 firmware (PlatformIO), with `test/run.sh` for the PC test.
- Needs Python 3 + Pillow. If Pillow has no raqm (often on Windows), `pip install python-bidi` for Hebrew order.

## Status and next steps

The cloud session that wrote the pack builder and firmware (branch `claude/esp-firmware-bird-pack-onwlzv`) could not reach the board, the photos or the PlatformIO toolchain. So **the firmware has never been compiled or run**; the logic and screens are tested on the PC, the rest is not.

1. **Phase 0, bring-up (not done, needs the board):** find the serial port, flash Waveshare's ESP32-S3-ePaper-1.54 demo, and record the pins in `PLAN.md` section 2 **and in `firmware/include/board_pins.h`** (EPD SCK/MOSI/CS/DC/RST/BUSY, I²C SDA/SCL, BOOT and PWR GPIOs, and, if present, power-latch, EPD-power and amplifier-enable pins). Check the display, SHTC3, PCF85063 and both buttons. Confirm the flash size (partitions assume 8 MB) and that `GxEPD2_154_D67` is the right driver class. Stretch: 4-grey mode.
2. **Phase 1, pack (builder done, photos not run):** `python3 tools/build_pack.py --photos <folder with the photos already on this PC>`. Ask the owner where those live; photos are matched by file name `<scientific_name_with_underscores>.jpg` (e.g. `motacilla_alba.jpg`), anything missing is downloaded from `data.json`'s `image` URL (the cloud session's network blocked Wikimedia, so that path is untested against the real site). Review `out/contact.png`; fix bad crops with `crops.json` (`{"Motacilla alba": [0.6, 0.4]}`) or `overrides/<slug>.jpg`; `out/report.txt` lists species without a photo. Cards land in `firmware/data/cards/` (git-ignored); `firmware/src/ui_assets.h` (Hebrew phrases, digit fonts, icons) is regenerated and committed.
3. **Phase 2, firmware (written, uncompiled):** `firmware/`, PlatformIO + pioarduino (Arduino-ESP32 3.x). Copy `include/secrets.example.h` to `secrets.h`, fill the pins, then `pio run -t upload` and `pio run -t uploadfs`. First try `pio run -e demo -t upload` (fixed data, serial `a`/`d`/`n`) to check the layout on the real panel against `mockups/`. Expect to fix compile errors and API mismatches: the code was only syntax-checked against stub headers.
4. Check the things listed under "Unverified" below.

## Firmware notes

- `src/logic.h` (newest-10, rotation, "ago" and "heard" phrases) and `src/screens.h` (screens A and D) have no Arduino dependency. `firmware/test/run.sh` builds them with g++, checks them against the real `data.json` (slug rule, newest-10 order, rotation, phrases), and renders both screens to `firmware/test/out/*.png`. `mockups/firmware-vs-mockup.png` shows that render (left) next to `tools/mockups.py` (right), with a synthetic stand-in photo.
- Wake logic (`src/main.cpp`): timer = fetch + next bird; BOOT = switch A/D without Wi-Fi; PWR = refresh now. If PWR turns out not to be a readable GPIO (`PIN_BTN_PWR -1`), holding BOOT for 1.5 s refreshes instead. After a button wake it stays awake 4 s so presses can follow each other.
- Always a full refresh (about 2 s), no partial refresh: a partial refresh after deep sleep depends on the controller's RAM state and is untested on this panel. Revisit after bring-up if the 5-minute cycle should be quieter.
- Species heard after the pack was built show their English name in a built-in font (no pack card, no Hebrew); rebuild and re-flash the pack to include them.
- The TLS certificate of `raw.githubusercontent.com` is not checked (`setInsecure`): the data is public and only drives the display.
- The amplifier pin is driven off and held through sleep, and the codec is never started.

### Unverified

Everything that touches hardware or the network: pins, SPI/I²C wiring, the `GxEPD2_154_D67` class, SHTC3 and PCF85063 register use, ext1 wake from BOOT/PWR, power-latch behaviour on battery, the Wi-Fi/HTTPS/ETag fetch with ArduinoJson's filter, `If-None-Match` behaviour of raw.githubusercontent.com, NTP → RTC, the Israel time-zone string, and the LittleFS partition fit (the pack is about 0.65 MB on a 4 MB partition).

## Repo notes

- The rest of this repo is the published BirdNET Kids site, updated hourly by an export. Keep all display work inside `home-display/`, and check how the export pushes before merging to `main`, so it can't wipe the folder.
- Show changes before merging: screenshots of the rendered screens (`mockups.py`), or photos of the real panel.

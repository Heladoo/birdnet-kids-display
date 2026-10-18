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
- Needs Python 3 + Pillow. If Pillow has no raqm (often on Windows), `pip install python-bidi` for Hebrew order.

## Next steps (the cloud session that wrote this could not reach the board)

1. **Phase 0, bring-up:** find the board's serial port, flash Waveshare's ESP32-S3-ePaper-1.54 demo, and record the pins (EPD SPI/CS/DC/RST/BUSY, I²C SDA/SCL for SHTC3 + PCF85063, BOOT and PWR button GPIOs, power-latch/enable pins) in `PLAN.md` section 2. Check the display, sensor, RTC and both buttons. Stretch: 4-grey mode.
2. **Phase 1, `tools/build_pack.py`:** ask the owner where the bird photos already live on this PC and use them first (download only what's missing), then write the cards and a contact sheet for review.
3. **Phase 2, firmware** (PlatformIO, Arduino-ESP32, GxEPD2, ArduinoJson) in `home-display/firmware/`, per `PLAN.md` section 7.

## Repo notes

- The rest of this repo is the published BirdNET Kids site, updated hourly by an export. Keep all display work inside `home-display/`, and check how the export pushes before merging to `main`, so it can't wipe the folder.
- Show changes before merging: screenshots of the rendered screens (`mockups.py`), or photos of the real panel.

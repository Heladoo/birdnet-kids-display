# BirdNET Kids display

Small physical displays for [BirdNET Kids](https://github.com/Heladoo/Birdnet_kids), the kids' web
interface of a BirdNET-Pi bird-sound station. Each display shows the birds heard most recently:
photo, Hebrew and English name, how often it was heard this week, and when. This repo is
separate from the website, which it only reads from.

## Devices

| Folder | Hardware | Status |
|---|---|---|
| [`devices/esp32-s3-epaper-1.54`](devices/esp32-s3-epaper-1.54) | Waveshare ESP32-S3-ePaper-1.54: 200×200 black/white e-paper, SHTC3, RTC, buttons, speaker | Working on the real board: photos, names, clock, temperature, battery, buttons, 5-minute cycle |
| [`devices/esp32-c6-touch-amoled-1.43`](devices/esp32-c6-touch-amoled-1.43) | Waveshare ESP32-C6-Touch-AMOLED-1.43: round 466×466 colour AMOLED, touch | Idea only |

Every device is a self-contained folder (firmware, tools, plan, notes). See the device's `PLAN.md`
for the design and `CLAUDE.md` for how to build and flash it.

## Where the data comes from

Everything is read from the website's public files, over the internet, so a display works on any
Wi-Fi: [`docs/data-contract.md`](docs/data-contract.md) lists exactly which fields and files are
used. The website's own repo is never written to from here.

## Sharing code between devices

Not extracted yet; with one device it would be guesswork. Candidates when the second one is
built: the `data.json` fetch and filter, the newest-10 and rotation logic (`logic.h`), the bird
cut-out step of the photo pipeline, and the badge and phrase tables.

## Licence

None chosen yet. Note for whoever picks one: the e-paper firmware uses GxEPD2 (GPL-3.0), so a
published firmware has to be GPL-3-compatible. The bird photos come from Wikimedia Commons under
their own licences and need credit; the pack builder downloads them, the generated cards are not
committed, and hand-picked ones are credited in each device's `overrides/CREDITS.md`.

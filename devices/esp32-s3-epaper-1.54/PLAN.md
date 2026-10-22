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
| **PWR** | **Next bird** from memory (no Wi-Fi, fast) | **Refresh now** (hold 1.5 s): reconnect, fetch, full refresh |
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

## 9. Sound POC (stages 0, 1 and 2 done)

Goal: hold BOOT for 1.5 s and hear the recording of the bird on screen, without keeping audio in RAM or flash.

**Facts**
- Audio path: ES8311 codec (control over I²C 0x18 on SDA 47 / SCL 48), I²S on MCLK 14, BCLK 15, WS 38, DOUT 45, DIN 16 (from the factory firmware's log), audio supply GPIO42 (active low), amplifier enable GPIO46 (a strapping pin; check polarity), speaker already connected. Waveshare's `02_Example/Arduino/08_Audio_Test` is the reference for the init sequence.
- Source clips: `audio/<slug>.mp3` in this repo (served by GitHub Pages): 411 files (up to 3 per species), MP3 44.1 kHz stereo 128 kbps, 4.5–6 s, 70–97 KB each, 36 MB in total (a few are empty files). One clip is about 97 KB; the whole set does not fit in flash.

**Approach: stream, don't store.** Fetch the clip over HTTPS in small chunks, decode MP3 on the fly (Helix decoder), write to I²S. RAM needed: ~8–16 KB input buffer + ~25 KB decoder + ~5 KB output frame + I²S DMA, about 40 KB of the ~270 KB free internal RAM (PSRAM, 8 MB, is there for a bigger jitter buffer if needed). The file is never held whole.

**Stages**
0. Beep: bring up ES8311 + I²S + amplifier and play a generated 440 Hz tone (nothing stored). Proves wiring, volume, amp polarity, pop on start/stop.
1. Stream one fixed clip from GitHub Pages and play it: proves Wi-Fi + decode + I²S together. Log the heap low-water mark and the time to first sound.
2. Wire it to the UI: BOOT held 1.5 s plays the clip of the bird shown on A (a short press still switches the screen). Any press stops it. Audio supply and amplifier are switched on only while playing and are off again before sleep; the screen is not redrawn while sound plays.
3. Polish: volume setting, which of the species' clips (up to 3), optional smaller files (22 kHz mono, ~30 KB each, re-encoded by the export), or an offline mode with all clips on the TF card.

**Risks:** current spike of Wi-Fi plus speaker on weak USB power (brownout reset); ES8311 register init (port Waveshare's `esp_codec_dev` setup); amplifier hiss while idle (keep the supply off); the 4 s awake window and the stay-awake mode must not cut playback.

**Done when:** the clip is recognisable, free heap stays above 100 KB during playback, nothing resets, and sound stops cleanly.

**Stage 0 result (2026-10-21): passed.** `pio run -e sound_beep -t upload` (source: `firmware/src/sound/`, own env so the normal builds do not include it). The ES8311 answers on 0x18 (chip id 83 11), I²S + MCLK on GPIO14 run, the init sequence writes without errors, and a 440/880 Hz tone is heard clearly from the speaker. Findings:
- Amplifier GPIO46 is active high and the codec supply GPIO42 active low, as in Waveshare's demo; supply and amplifier are switched on only for the beep and are off again afterwards.
- Volume (codec DAC register, dB): -30 dB is too quiet to judge, -18 dB is clearly audible, -6 dB is loud. A default of about -15 dB looks right; revisit with real bird clips (they are loudness-normalised already).
- A minimal driver (`es8311_min.h`, ported from Espressif's esp_codec_dev, Apache-2.0) is enough; no need to vendor the whole library. 16, 22.05, 32, 44.1 and 48 kHz are in its table; only 16 kHz has been heard so far.

**Stage 1 result (2026-10-21): passed.** `pio run -e sound_stream -t upload` (source: `firmware/src/sound/stream_test.cpp`, `audio_out.h`; needs `include/secrets.h`). Plays `https://heladoo.github.io/Birdnet_kids/audio/<slug>.mp3` over HTTPS: GitHub Pages answers with a Content-Length (no chunking), the Helix decoder (`pschatzmann/arduino-libhelix` v0.9.4) turns 44.1 kHz stereo 128 kbit/s MP3 into PCM, the I2S DMA plays it. Volume -15 dB was right.
- **First try stuttered.** With only the 5.7 KB TCP buffer ahead of the decoder, Wi-Fi bursts (gaps up to 200 ms at -70 dBm) ran the audio dry: 88 stalls, 6.06 s of sound took 6.7 s.
- **Fix: jitter buffer in PSRAM.** A 160 KB ring (PSRAM is otherwise unused) is filled between decode steps; playback starts once 32 KB (about 2 s of audio) are in. Result: first sound 0.8 s after the request, the ring never ran dry (lowest fill 33 KB), 6.06 s of sound in 6.16 s, no audible gaps.
- **Memory:** internal heap fell from 296 KB free to a 188 KB minimum (TLS handshake plus decoder, about 110 KB) and is back to 253 KB after; the clip itself lives only in PSRAM, so the clip size does not matter for internal RAM.
- **Timing:** joining Wi-Fi took 6.6-8.3 s at -68..-71 dBm, so a button press that has to connect first waits that long before the first sound; staying connected, or connecting while the screen redraws, would hide it.
- Licences: the Helix wrapper is GPL-3 (the repo's firmware has to be GPL-3-compatible anyway because of GxEPD2); the decoder itself is RealNetworks RPSL.

**Stage 2 result (2026-10-22): works in the normal firmware.** Hold BOOT 1.5 s: a short chirp confirms the hold, the clip of the bird on screen plays (recordings `<slug>.mp3`, `_2`, `_3` in turn; a missing one is skipped), any new press cuts it short. A short BOOT press still switches the screen. Code: `src/sound/clip_player.h` (player, chirp), `audio_out.h`, `es8311_min.h`; `main.cpp` has `playCurrentBird()`; env `bird_clips` is the normal firmware plus clip statistics on serial.
- **No visual feedback on purpose:** a speaker icon needed two extra full e-paper refreshes (about 2 s each) and looked bad. The audible chirp replaces it. The first chirp (70 ms, no lead-in) was inaudible because the codec ramps its output up after unmuting; 100 ms of lead-in, 160 ms of tone and amplitude 26000 fixed that.
- **Time from hold to sound is about 12 s** (accepted for now). Costs: the 1.5 s hold, Wi-Fi connect (6-8 s measured before, less with the remembered channel and access point, `st.wifiChan/wifiBssid`), TLS handshake and headers 0.5-2.6 s depending on the connection, 0.8 s prefill. Ideas to cut it: keep Wi-Fi up longer (it is already left on for 60 s after a clip in the USB-awake mode), pre-connect Wi-Fi when BOOT is first touched, lower the prefill to 16 KB, keep the TLS session, or a smaller clip format.
- **Bugs found with the serial log and fixed:** the stop check used to fire while the starting hold was still down (clips cut off at once, then the next recording was tried); a press now only counts after every button was seen released, and a cancel ends everything. A 404 for `_2`/`_3` resets the rotation to the first recording. Only 1 of 411 clips is empty (`corvus_monedula_3.mp3`).
- Wi-Fi is left on for 60 s after a clip when a PC is attached, and switched off before deep sleep.
- The green LED (GPIO3, active low, from the schematic and Waveshare's sleep demo) blinks at 2 Hz from the recognised hold to the end of the clip, as feedback that replaces an e-paper icon. It runs in its own FreeRTOS task because playback blocks, and is held off in deep sleep.

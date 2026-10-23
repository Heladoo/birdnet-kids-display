# How the e-paper display works

A desk display that shows the birds heard most recently by the BirdNET Kids station. Everything
below is about the current firmware (`firmware/src/main.cpp`). `PLAN.md` has the design history and
`CLAUDE.md` the build and flash notes.

## The big picture

```
 BirdNET-Pi (Raspberry Pi) --hourly export--> GitHub: Heladoo/Birdnet_kids  (data.json, news.json, audio/*.mp3)
                                                      |
                              Wi-Fi + HTTPS, only when the device needs it
                                                      v
                          ESP32-S3 on the Waveshare e-paper board
        flash: the 161 bird cards (photo + Hebrew/English name), fonts and icons
        RTC memory: the latest 10 birds, which one is shown, last sensor reading, Wi-Fi channel
        e-paper: shows screen A (the bird) or D (today: weather); keeps the image with no power
```

The device **sleeps almost all the time**. It wakes every 20 minutes, changes the bird, updates the clock,
temperature and battery, redraws, and sleeps again, **without using Wi-Fi**. Once an hour (about 10 minutes
past the hour, after the site's hourly export) it also connects and asks for new birds. A button press
wakes it at any time. Nothing runs on the Pi for the display.

## When does it connect to Wi-Fi?

| Moment | What it does | Typical cost |
|---|---|---|
| Every 20-minute wake | **no Wi-Fi**: next bird from memory, clock, sensor, battery, redraw | about 3 s awake |
| Once an hour, about 10 minutes past | connect, ask GitHub whether `data.json` changed (ETag), download and parse it if it did (it usually has, the site exports hourly), then ask Open-Meteo for today's forecast (about 1.1 KB), disconnect | about 9-11 s |
| Once a day | piggybacks on that connection to set the clock (NTP, Israel time zone) | +1 s |
| PWR held 1.5 s | the same fetch, on demand | |
| BOOT held 1.5 s (sound) | connects, streams the bird's MP3 from GitHub Pages, plays it; Wi-Fi stays on a short while so the next press is quick | about 12 s to the first sound |
| Power-on / reset | one full fetch | |
| Short button presses | **no Wi-Fi**: they only switch the screen or step to the next stored bird | about 2 s (e-paper redraw) |

So a new bird appears on the display at most about an hour and ten minutes after it is heard (the
site's export is hourly; asking more often could not show it sooner).

With a PC on the USB port the device does not sleep (so USB stays connected); it follows the same
20-minute rhythm and the same once-an-hour fetch, and turns Wi-Fi off between them.

Wi-Fi uses the name and password in `firmware/include/secrets.h` (git-ignored), compiled in. It
remembers the channel and access point of the last connection, which skips the scan.

## What is stored where

| Memory | Holds | Survives |
|---|---|---|
| **Flash, 8 MB** | firmware (about 1.2 MB); the **bird pack** (161 cards, 525 KB: each has the cut-out photo and the Hebrew and English names); phrases, digit fonts and icons are compiled in | everything, including power loss |
| **RTC memory** (a few KB, kept in deep sleep) | the **newest 10 shown birds** (scientific and English name, badges, last heard, count this week), **today's forecast** (24 hourly temperatures, rain chances and weather pictures), which screen and which bird are shown, the last good temperature/humidity, the data `ETag`, Wi-Fi channel and access point, the time of the last clock sync | deep sleep (not a full power loss) |
| **Internal RAM, about 330 KB** | working memory: the JSON parse (the 249 KB file is filtered while it streams in, so only about 10 KB of it is kept), the TLS connection (about 110 KB peak), one card at a time | nothing (rebuilt each wake) |
| **PSRAM, 8 MB** | only a 160 KB buffer while a bird sound plays, then freed | nothing |
| **Clock chip (PCF85063)** | the time of day | as long as it has power |

Only the **newest 10 birds that may be shown** are kept. A bird may be shown when the site is confident about
it (no "Low confidence" badge, same as the website's "Confident" filter), it has a Hebrew name, and the pack
has its card (which needs a photo). A bird heard after the pack was built, or one with no Hebrew name yet,
is simply not shown until the pack is rebuilt and uploaded. Photos never travel over Wi-Fi: they are in flash.

## What is polled again and again

- Once an hour: the forecast from Open-Meteo, and `data.json` with `If-None-Match`. When nothing changed GitHub answers 304 with no body
  (seen on the device itself and with `curl`: 0 bytes in 0.2 s, against 249 KB for a full download).
- Every 20 minutes, with no radio: the temperature/humidity sensor (retried once if a read fails), the
  battery voltage, the clock chip.
- Buttons are **not polled** while asleep: pressing one wakes the chip (hardware wake-up).
- Only with a PC on USB (no sleep): buttons every 20 ms, "is the PC still there" every 2 s.

## When there is no Wi-Fi

- The device keeps working from what it has: the stored 10 birds keep rotating every 20 minutes, the
  photos and names are in flash, the clock runs on the clock chip, temperature, humidity and battery
  still update. The "heard 7 min ago" text keeps moving with the clock. New birds simply do not
  appear until Wi-Fi is back.
- **Sound does not work** (it streams from the internet).
- If the clock was never set (first power-up with no Wi-Fi) the time shows `--:--`.
- After a failed attempt (no Wi-Fi, or no answer) it waits 2 wakes (about 40 minutes) before trying
  again, so a missing network does not drain the battery. Each failed attempt waits up to 12 s for Wi-Fi.
- On a different network the device needs the new Wi-Fi name and password: today that means editing
  `secrets.h` and flashing over USB. A setup mode is on the backlog.

## Battery life (estimates, not measurements)

The battery size is not known, so the numbers are per 1000 mAh, from typical ESP32-S3 currents. The
real figures depend on the board's sleep current (assumed 0.2 mA), which has not been measured yet.

| Use | Estimated drain | Per 1000 mAh |
|---|---|---|
| **Idle**: only the automatic cycle (72 wakes of about 0.03 mAh without Wi-Fi, 24 hourly fetches of about 0.2 mAh, plus sleeping) | about 12 mAh/day | **about 11 weeks** (range 7 to 14) |
| **Active**: the above plus 30 button presses and 20 sounds a day (a sound is about 0.4 mAh, a press about 0.07 mAh) | about 22 mAh/day | **about 6 weeks** |
| Heavy kid use: 100 sounds a day | about 52 mAh/day | about 19 days |

(Before the schedule change, with a Wi-Fi fetch on every 5-minute wake, idle was about 40 mAh/day:
about 3 weeks.) Where the idle drain goes now: sleeping about 40 %, the hourly fetches about 40 %, the
redraws about 20 %. So the next lever is the board's own sleep current (for example the panel's supply is
held on while asleep), and the only way to know it is a real measurement: run it on battery with the
voltage logged each wake and read the log over USB.

## An SD card, what it would give

The board has a microSD slot (not used yet).

- **Sound without Wi-Fi:** all 411 clips are 36 MB, which fits an SD card. Playback starts in about
  a second instead of 12, and a sound costs a fraction of the battery.
- **More and better photos:** several pictures per species, or higher resolution, without the 4 MB
  flash limit; a shared library if a second display is added.
- **Offline data:** a cached `data.json` and `news.json`, so the display shows something sensible
  after a long Wi-Fi outage.
- **Setup without flashing:** Wi-Fi name and password in a text file on the card, several networks.
- **Logs:** battery voltage over time, errors, how often it ran: real battery numbers.
- **Cards for new species:** the device could fetch and cache them.
- Costs to check: a card draws a little current even when idle, which matters while asleep.

## Screens

**A, the bird:** the photo, Hebrew and English name, "heard 17 times this week", badges, "7 minutes ago".

**D, today:** the weekday and date in Hebrew (for example "יום ו׳, 23 באוקטובר"), a picture of the weather (sunny,
partly cloudy, cloudy, rain, storm, haze, wind, clear night) with the day's highest and lowest temperature, a
graph of today's temperature by hour with the chance of rain as small bars and a marker at the current hour, and a
dotted box left free for daily notes (nothing writes into it yet). The forecast comes from
[Open-Meteo](https://open-meteo.com) (free, no key) for the station's location rounded to 0.05 degrees (about
5 km), fetched with the hourly Wi-Fi session. If the stored forecast is not for today (no Wi-Fi for a long time),
the picture and graph are left out and only the date and the notes box show. The top bar of both screens shows
the indoor temperature and humidity from the board's own sensor, a plug or battery symbol, and the clock.

BOOT (short) switches between A and D. The earlier "newest 10" list screen was replaced by D. A mockup of a
possible news screen (new bird, welcome back) is kept for later: see `BACKLOG.md` and `mockups/news-3.png`.

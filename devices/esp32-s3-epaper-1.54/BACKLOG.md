# Backlog

Ideas and open work, roughly by value. Done items are in the git history; `HOW_IT_WORKS.md` describes what exists.

## Next

- **News screen** (kept for later). Mockups: `mockups/news-2.png` and `mockups/news-3.png` (from
  `tools/mockup_news.py`). Items come from the site's `news.json` (247 bytes: "New bird! First time ever",
  "Welcome back! Back after 64 days away"), each with a small photo and the Hebrew name. Shown only when there is
  news; the device would fetch `news.json` with the hourly data fetch.
- **Daily notes** for the box on the Today screen: decide where they come from (a small `notes.json` in a repo,
  or the website) and fetch them hourly.
- **Measure the battery:** log the voltage each wake, run a few days on battery, read the log over USB. The idle
  estimate (about 11 weeks per 1000 mAh) is only an estimate; the board's own sleep current is the biggest unknown
  (for example the panel supply is held on while asleep).
- **Cut the hold-to-sound delay** (about 12 s): connect Wi-Fi when BOOT is first touched, a smaller prefill, reuse
  the TLS session.
- **Wi-Fi without reflashing:** a setup mode (an access point and a page) or a `wifi.txt` on an SD card, several
  networks.
- **Cards for new species without a rebuild:** publish the pack and let the device download a missing card; add
  Hebrew names for new species on the site (Zitting Cisticola and Remiz pendulinus have none yet).

## Ideas

- A quiz screen: the photo without the name, a press reveals it, BOOT held plays the song.
- A microSD card: all 411 clips offline (sound in about a second, no Wi-Fi), more photos per species, a cached
  `data.json`, logs. Check its idle current while the device sleeps.
- The colour AMOLED sibling (`devices/esp32-c6-touch-amoled-1.43`).
- Photos of birds that are hidden as "Low confidence" are not reviewed; if they become confident, check them
  (`tools/find_photos.py`).
- Verify on the device over time: the clock after a power loss, the change of daylight saving time, a long run.

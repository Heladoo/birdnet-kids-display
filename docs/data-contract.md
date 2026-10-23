# Data contract with the BirdNET Kids site

The displays read these public files from the site repo `Heladoo/Birdnet_kids` (branch `main`,
also served at https://heladoo.github.io/Birdnet_kids/). The site is exported hourly from a
Raspberry Pi. If the site changes these, the displays break, so change them together.

| File | Used by | What is used |
|---|---|---|
| `data.json` (~250 KB) | firmware at run time, pack builders | array of species; per species: `sci`, `name` (English), `he_name`, `last_seen` (local time string), `week_count`, `badges[].en`, `image` (Wikimedia URL) |
| `expert-data.json` | pack builders | `sci`, `com` (English), `he`, for species that are not in `data.json` |
| `audio/<sci_slug>.mp3` (+ `_2`, `_3`) | sound proof of concept (planned) | best normalised clips; MP3 44.1 kHz stereo 128 kbps, 70–97 KB; a few files are empty |

Raw URL used by the firmware: `https://raw.githubusercontent.com/Heladoo/Birdnet_kids/main/data.json`
(HTTPS, answers `ETag`, so an unchanged file costs a tiny 304).

The badge named `Low confidence` (icon ❓) in `badges[]` marks a species the site is unsure about; the website's "Confident" filter hides those, and so do the displays (never show or play them).

A bird is shown on a display only if it is not marked "Low confidence", `he_name` is not empty, and the pack has its
card (which needs a photo from `image`).

## Not from the site: the weather

The Today screen uses one other service, once an hour: Open-Meteo's forecast API
(`https://api.open-meteo.com/v1/forecast`, hourly `temperature_2m`, `precipitation_probability`, `weather_code`,
`is_day`, one day, time zone Asia/Jerusalem) for the station's location rounded to 0.05 degrees (31.50, 34.65 for
Dorot). WMO weather codes map to eight pictures in `logic::weatherKind`.

`<sci_slug>` is the scientific name lowercased, runs of non-alphanumerics replaced by `_`
(`Motacilla alba` becomes `motacilla_alba`).

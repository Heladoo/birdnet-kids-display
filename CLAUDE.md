# CLAUDE.md

Displays for BirdNET Kids. Read `README.md` for the layout; each device folder under `devices/`
is self-contained and has its own `CLAUDE.md` (status, how to build and flash) and `PLAN.md`.

- Start in the device's `CLAUDE.md`. Cross-device facts go in `docs/`.
- The website lives in a **different repo** (`Heladoo/Birdnet_kids`), which is fed by an hourly
  cron on the Raspberry Pi that pushes to its `main` without pulling. **Never push here to that
  repo**; this repo only reads its published files (`docs/data-contract.md`).
- Everything for the displays runs on the PC: building packs, flashing. Nothing runs on the Pi.
- Wi-Fi details live in each device's git-ignored `secrets.h`; never commit them.
- Commit locally as you go; push only when asked.

# ESP32-C6-Touch-AMOLED-1.43 (idea, not started)

A colour sibling of the e-paper display, on the Waveshare ESP32-C6-Touch-AMOLED-1.43:
466×466 round AMOLED (SH8601 over QSPI), FT6146 touch, LVGL 9, no PSRAM, Wi-Fi 6.

What would carry over from `../esp32-s3-epaper-1.54`: the data fetch and filter, the newest-10 and
rotation logic, the bird cut-out. What would not: the 1-bit dithering and the full-refresh
cycle. With colour and touch, swipe could replace the buttons. No PSRAM means the photos need
streaming or pre-sized storage; plan that first.

When this is started, extract the shared pieces into a common folder (see the top README).

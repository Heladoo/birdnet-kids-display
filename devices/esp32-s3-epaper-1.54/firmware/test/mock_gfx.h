// The few Adafruit_GFX calls screens.h uses, drawing into a 200x200 buffer on the PC.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct MockGfx {
  static constexpr int W = 200, H = 200;
  uint8_t px[W * H];  // 1 = white, 0 = black

  void fillScreen(uint16_t c) { memset(px, c ? 1 : 0, sizeof px); }
  void set(int x, int y, uint16_t c) {
    if (x >= 0 && x < W && y >= 0 && y < H) px[y * W + x] = c ? 1 : 0;
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) set(x + i, y + j, c);
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void drawPixel(int16_t x, int16_t y, uint16_t c) { set(x, y, c); }
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) {  // Bresenham
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
      set(x0, y0, c);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
  // Same contract as Adafruit_GFX::drawBitmap: only the set bits are drawn, in `c`.
  void drawBitmap(int16_t x, int16_t y, const uint8_t* bm, int16_t w, int16_t h, uint16_t c) {
    int rb = (w + 7) / 8;
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++)
        if (bm[j * rb + i / 8] & (0x80 >> (i & 7))) set(x + i, y + j, c);
  }
  bool writePbm(const char* path) const {  // P4: 1 = black, rows padded to bytes
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P4\n%d %d\n", W, H);
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x += 8) {
        uint8_t b = 0;
        for (int i = 0; i < 8 && x + i < W; i++)
          if (!px[y * W + x + i]) b |= 0x80 >> i;
        fputc(b, f);
      }
    fclose(f);
    return true;
  }
};

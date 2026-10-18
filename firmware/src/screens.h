// Screens A (the bird) and D (the latest 10), drawn with Adafruit_GFX-style calls
// so they run on the real panel (GxEPD2) and in the PC test (test/mock_gfx.h),
// where they are compared with tools/mockups.py. Layout numbers copy mockups.py.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "logic.h"
#include "ui_assets.h"
#ifdef ARDUINO
#include <Fonts/FreeSansBold9pt7b.h>
#endif

namespace screens {

constexpr uint16_t kBlack = 0x0000, kWhite = 0xFFFF;
constexpr int W = 200;
constexpr int kPhotoH = 108, kHeBigH = 24, kEnH = 14, kHeSmallH = 16;
constexpr size_t kCardMax = 4096;

// One species' card from the pack (tools/build_pack.py documents the format).
struct Card {
  uint8_t heBigW, enW, heSmallW;
  const uint8_t *photo, *heBig, *en, *heSmall;  // point into the buffer it was parsed from
};

inline bool parseCard(const uint8_t* b, size_t len, Card& c) {
  if (len < 8 || memcmp(b, "BKC1", 4) != 0) return false;
  c.heBigW = b[4];
  c.enW = b[5];
  c.heSmallW = b[6];
  size_t o = 8;
  c.photo = b + o;
  o += (W / 8) * kPhotoH;
  c.heBig = b + o;
  o += ((c.heBigW + 7) / 8) * kHeBigH;
  c.en = b + o;
  o += ((c.enW + 7) / 8) * kEnH;
  c.heSmall = b + o;
  o += ((c.heSmallW + 7) / 8) * kHeSmallH;
  return o <= len;
}

struct Env {
  bool sensorOk = false;
  float temp = 0, hum = 0;
  bool clockOk = false;
  int hh = 0, mm = 0;
};

// ---------------------------------------------------------------- text

inline int glyphIndex(char c) {
  const char* p = strchr(kDigitCharset, c);
  return p ? (int)(p - kDigitCharset) : -1;
}
inline int textWidth(const DigitFont& f, const char* s) {
  int w = 0;
  for (; *s; ++s) {
    int i = glyphIndex(*s);
    if (i >= 0) w += f.g[i].w;
  }
  return w;
}
// Digits and ':' '%' '-' 'o'(degree), centered vertically on yc.
template <class G>
void drawText(G& g, const DigitFont& f, const char* s, int x, int yc, uint16_t color) {
  int y = yc - f.h / 2;
  for (; *s; ++s) {
    int i = glyphIndex(*s);
    if (i < 0) continue;
    g.drawBitmap(x, y, f.g[i].bits, f.g[i].w, f.h, color);
    x += f.g[i].w;
  }
}
template <class G>
void drawStrip(G& g, const Strip& s, int x, int y, uint16_t color) {
  g.drawBitmap(x, y, s.bits, s.w, s.h, color);
}

// A Hebrew phrase around a live number, laid out right to left and centered
// ("נשמע" 284 "פעמים השבוע") or right-aligned: pre | number | post.
template <class G>
void drawAroundNumber(G& g, const Strip& pre, const char* num, const DigitFont& f, const Strip& post,
                      int yc, bool centered, int xRight, uint16_t color) {
  constexpr int kGap = 4;
  int nw = textWidth(f, num);
  int total = pre.w + kGap + nw + kGap + post.w;
  int x = centered ? (W - total) / 2 : xRight - total;
  int top = yc - pre.h / 2;
  drawStrip(g, post, x, top, color);
  drawText(g, f, num, x + post.w + kGap, yc, color);
  drawStrip(g, pre, x + post.w + kGap + nw + kGap, top, color);
}

// -------------------------------------------------------------- top bar

template <class G>
void topBar(G& g, const Env& e) {
  g.fillRect(0, 0, W, 20, kBlack);
  char buf[16];
  g.drawBitmap(3, 4, ui::ICON_THERMO, 12, 12, kWhite);
  if (e.sensorOk) snprintf(buf, sizeof buf, "%do", (int)lroundf(e.temp)); else strcpy(buf, "--o");
  drawText(g, ui::BAR, buf, 16, 10, kWhite);
  g.drawBitmap(52, 4, ui::ICON_DROP, 12, 12, kWhite);
  if (e.sensorOk) snprintf(buf, sizeof buf, "%d%%", (int)lroundf(e.hum)); else strcpy(buf, "--%");
  drawText(g, ui::BAR, buf, 65, 10, kWhite);
  if (e.clockOk) snprintf(buf, sizeof buf, "%02d:%02d", e.hh, e.mm); else strcpy(buf, "--:--");
  drawText(g, ui::CLOCK, buf, W - 4 - textWidth(ui::CLOCK, buf), 10, kWhite);
}

// -------------------------------------------------------------- screen A

template <class G>
void drawBird(G& g, const Card* card, const logic::Bird& b, time_t now, const Env& e) {
  g.fillScreen(kWhite);
  if (card) {
    g.drawBitmap(0, 20, card->photo, W, kPhotoH, kBlack);
    g.drawBitmap((W - card->heBigW) / 2, 129, card->heBig, card->heBigW, kHeBigH, kBlack);
    g.drawBitmap((W - card->enW) / 2, 152, card->en, card->enW, kEnH, kBlack);
  }
#ifdef ARDUINO
  else {  // not in the pack (heard after it was built): English name in a built-in font
    g.setFont(&FreeSansBold9pt7b);
    g.setTextColor(kBlack);
    int16_t x1, y1;
    uint16_t w, h;
    g.getTextBounds(b.name, 0, 0, &x1, &y1, &w, &h);
    g.setCursor((W - (int)w) / 2 - x1, 150);
    g.print(b.name);
  }
#endif
  topBar(g, e);

  switch (logic::heardKind(b.week)) {
    case logic::Heard::None:  drawStrip(g, ui::HEARD_NONE, (W - ui::HEARD_NONE.w) / 2, 166, kBlack); break;
    case logic::Heard::Once:  drawStrip(g, ui::HEARD_ONCE, (W - ui::HEARD_ONCE.w) / 2, 166, kBlack); break;
    case logic::Heard::Twice: drawStrip(g, ui::HEARD_TWICE, (W - ui::HEARD_TWICE.w) / 2, 166, kBlack); break;
    case logic::Heard::Many: {
      char n[8];
      snprintf(n, sizeof n, "%u", (unsigned)b.week);
      drawAroundNumber(g, ui::HEARD_PRE, n, ui::WEEK, ui::HEARD_POST, 174, true, 0, kBlack);
      break;
    }
  }
  g.drawFastHLine(8, 183, W - 16, kBlack);

  int x = 6;
  for (int i = 0; i < 3; i++) {
    const uint8_t* icon = ui::iconForBadge(b.badge[i]);
    if (!icon) continue;
    g.drawBitmap(x, 187, icon, 12, 12, kBlack);
    x += 15;
  }

  logic::Ago a = logic::ago(now, (time_t)b.seen);
  const int xr = W - 6, yc = 193;
  char n[8];
  snprintf(n, sizeof n, "%d", a.value);
  switch (a.kind) {
    case logic::AgoKind::Min:   drawAroundNumber(g, ui::AGO_PRE, n, ui::AGO, ui::AGO_MIN, yc, false, xr, kBlack); break;
    case logic::AgoKind::Hours: drawAroundNumber(g, ui::AGO_PRE, n, ui::AGO, ui::AGO_HOURS, yc, false, xr, kBlack); break;
    case logic::AgoKind::Hour:  drawStrip(g, ui::AGO_HOUR, xr - ui::AGO_HOUR.w, yc - 7, kBlack); break;
    case logic::AgoKind::Day:   drawStrip(g, ui::AGO_DAY, xr - ui::AGO_DAY.w, yc - 7, kBlack); break;
  }
}

// -------------------------------------------------------------- screen D

// `load(sci, card)` fills `card` for a species, valid until the next call; false = not in the pack.
template <class G, class Loader>
void drawList(G& g, const logic::Bird* top, int n, int sel, const Env& e, Loader load) {
  g.fillScreen(kWhite);
  topBar(g, e);
  drawStrip(g, ui::TITLE_LIST, (W - ui::TITLE_LIST.w) / 2, 23, kBlack);
  int y = 41;
  for (int k = 0; k < n; k++, y += 16) {
    uint16_t ink = kBlack;
    if (k == sel) {
      g.fillRect(0, y, W, 16, kBlack);
      ink = kWhite;
    }
    Card c;
    if (load(top[k].sci, c)) {
      g.drawBitmap(W - 4 - c.heSmallW, y, c.heSmall, c.heSmallW, kHeSmallH, ink);
    }
#ifdef ARDUINO
    else {
      g.setFont(&FreeSansBold9pt7b);
      g.setTextColor(ink);
      int16_t x1, y1;
      uint16_t w, h;
      g.getTextBounds(top[k].name, 0, 0, &x1, &y1, &w, &h);
      g.setCursor(W - 4 - (int)w - x1, y + 13);
      g.print(top[k].name);
    }
#endif
    struct tm lt;
    time_t seen = (time_t)top[k].seen;
    localtime_r(&seen, &lt);
    char t[8];
    snprintf(t, sizeof t, "%02d:%02d", lt.tm_hour, lt.tm_min);
    drawText(g, ui::LIST, t, 4, y + 8, ink);
  }
}

}  // namespace screens

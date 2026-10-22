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
  int batPct = -1;  // -1 = no battery: no icon
  bool plugged = false;  // a cable is in: the plug symbol replaces the battery icon
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
  if (e.plugged) {  // a plug: two prongs, a body and the cable (the battery icon shows only when unplugged)
    g.fillRect(124, 6, 4, 2, kWhite);
    g.fillRect(124, 12, 4, 2, kWhite);
    g.fillRect(128, 4, 9, 12, kWhite);
    g.fillRect(137, 8, 7, 4, kWhite);
  } else if (e.batPct >= 0) {  // battery: outline, nub and a fill of 16 px at 100%
    const int x = 124, y = 5;
    g.drawRect(x, y, 20, 10, kWhite);
    g.fillRect(x + 20, y + 3, 2, 4, kWhite);
    int fill = (e.batPct * 16 + 50) / 100;
    if (fill < 1 && e.batPct > 0) fill = 1;
    if (fill > 0) g.fillRect(x + 2, y + 2, fill, 6, kWhite);
  }
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

// Screen D shows only the latest kListRows birds, in large type (the Hebrew name is the
// big strip from the card, the time uses the CLOCK digits). A name too wide for the row
// falls back to the small strip. The bird on screen A is highlighted when it is in the list.
constexpr int kListRows = 5;
constexpr int kListY = 42, kListRowH = 31;

// `load(sci, card)` fills `card` for a species, valid until the next call; false = not in the pack.
template <class G, class Loader>
void drawList(G& g, const logic::Bird* top, int n, int sel, const Env& e, Loader load) {
  g.fillScreen(kWhite);
  topBar(g, e);
  drawStrip(g, ui::TITLE_LIST, (W - ui::TITLE_LIST.w) / 2, 23, kBlack);
  const int rows = n < kListRows ? n : kListRows;
  // The window follows the selection: beyond the first page the list scrolls so the
  // selected bird is the last row.
  const int start = sel >= rows ? sel - rows + 1 : 0;
  for (int r = 0; r < rows; r++) {
    const int k = start + r;
    const int y = kListY + r * kListRowH;
    uint16_t ink = kBlack;
    if (k == sel) {
      g.fillRect(0, y, W, kListRowH - 1, kBlack);
      ink = kWhite;
    }
    struct tm lt;
    time_t seen = (time_t)top[k].seen;
    localtime_r(&seen, &lt);
    char t[8];
    snprintf(t, sizeof t, "%02d:%02d", lt.tm_hour, lt.tm_min);
    const int tw = textWidth(ui::CLOCK, t);
    drawText(g, ui::CLOCK, t, 4, y + (kListRowH - 1) / 2, ink);

    Card c;
    if (load(top[k].sci, c)) {
      const int room = W - 4 - (4 + tw + 6);  // between the time and the right edge
      if (c.heBigW <= room) {
        g.drawBitmap(W - 4 - c.heBigW, y + (kListRowH - 1 - kHeBigH) / 2, c.heBig, c.heBigW, kHeBigH, ink);
      } else {
        g.drawBitmap(W - 4 - c.heSmallW, y + (kListRowH - 1 - kHeSmallH) / 2, c.heSmall, c.heSmallW, kHeSmallH, ink);
      }
    }
#ifdef ARDUINO
    else {
      g.setFont(&FreeSansBold9pt7b);
      g.setTextColor(ink);
      int16_t x1, y1;
      uint16_t w, h;
      g.getTextBounds(top[k].name, 0, 0, &x1, &y1, &w, &h);
      g.setCursor(W - 4 - (int)w - x1, y + 20);
      g.print(top[k].name);
    }
#endif
  }
}

}  // namespace screens

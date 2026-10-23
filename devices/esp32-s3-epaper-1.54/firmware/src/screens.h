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

// ------------------------------------------------------- screen D: today (weather)

// "27 / 17" (highest / lowest of the day), in the top bar's digits.
template <class G>
void drawHiLo(G& g, int hi, int lo, int x, int yc) {
  char a[8], b[8];
  snprintf(a, sizeof a, "%do", hi);
  snprintf(b, sizeof b, "%do", lo);
  drawText(g, ui::BAR, a, x, yc, kBlack);
  x += textWidth(ui::BAR, a) + 3;
  drawText(g, ui::BAR, "/", x, yc, kBlack);
  x += textWidth(ui::BAR, "/") + 3;
  drawText(g, ui::BAR, b, x, yc, kBlack);
}

// Temperature curve over 0..24 h with rain-chance bars, the day's highest and lowest, and a marker
// at the current time (`nowTenths` = hours x 10). Mirrors tools/mockup_weather.py.
template <class G>
void drawGraph(G& g, const logic::WeatherDay& w, int nowTenths, int x0, int y0, int x1, int y1) {
  int lo = w.temp[0], hi = w.temp[0], iLo = 0, iHi = 0;
  for (int i = 1; i < 24; i++) {
    if (w.temp[i] < lo) { lo = w.temp[i]; iLo = i; }
    if (w.temp[i] > hi) { hi = w.temp[i]; iHi = i; }
  }
  const int tlo = lo - 2, thi = hi + 2;
  const int top = y0 + 12, bot = y1 - 12, base = y1 - 10;
  auto X = [&](int tenths) { return x0 + (x1 - x0) * tenths / 240; };
  auto Y = [&](int t) { return bot - (bot - top) * (t - tlo) / (thi - tlo); };

  g.drawFastHLine(x0, base, x1 - x0 + 1, kBlack);
  static const int marks[5] = {0, 6, 12, 18, 24};
  for (int m : marks) {  // hour marks; the end ones are aligned inward so they are not cut off
    const int x = X(m * 10);
    g.drawFastVLine(x, base, 3, kBlack);
    char b[4];
    snprintf(b, sizeof b, "%d", m);
    const int tw = textWidth(ui::TINY, b);
    drawText(g, ui::TINY, b, m == 0 ? x : (m == 24 ? x - tw : x - tw / 2), y1 - 3, kBlack);
  }
  for (int i = 0; i < 24; i++) {  // rain chance, 12 px at 100 %
    const int hgt = (12 * w.rain[i] + 50) / 100;
    if (hgt > 0) {
      int bw = X(i * 10 + 10) - X(i * 10) - 2;
      g.fillRect(X(i * 10) + 1, base - hgt, bw > 1 ? bw : 1, hgt, kBlack);
    }
  }
  int px[24], py[24];
  for (int i = 0; i < 24; i++) {
    px[i] = X(i * 10 + 5);
    py[i] = Y(w.temp[i]);
  }
  for (int i = 0; i < 23; i++) {  // light shading under the curve: a quarter of the dots
    for (int x = px[i]; x < px[i + 1]; x++) {
      if (x % 2) continue;
      const int yc = py[i] + (py[i + 1] - py[i]) * (x - px[i]) / (px[i + 1] - px[i]);
      for (int yy = yc + 2; yy <= bot; yy++)
        if (yy % 2 == 0) g.drawPixel(x, yy, kBlack);
    }
  }
  for (int i = 0; i < 23; i++) {  // the curve, 2 px thick
    g.drawLine(px[i], py[i], px[i + 1], py[i + 1], kBlack);
    g.drawLine(px[i], py[i] + 1, px[i + 1], py[i + 1] + 1, kBlack);
  }
  char t[8];
  snprintf(t, sizeof t, "%do", hi);
  drawText(g, ui::TINY, t, px[iHi] - textWidth(ui::TINY, t) / 2, py[iHi] - 3 - ui::TINY.h / 2, kBlack);
  snprintf(t, sizeof t, "%do", lo);
  drawText(g, ui::TINY, t, px[iLo] - textWidth(ui::TINY, t) / 2, py[iLo] - 3 - ui::TINY.h / 2, kBlack);
  if (nowTenths < 0) nowTenths = 0;
  if (nowTenths > 239) nowTenths = 239;
  const int xn = X(nowTenths), yn = Y(w.temp[nowTenths / 10]);
  for (int yy = top - 2; yy < base; yy += 4) g.drawFastVLine(xn, yy, 2, kBlack);  // dashed "now" line
  g.fillRect(xn - 3, yn - 3, 7, 7, kBlack);  // and a ringed dot on the curve
  g.fillRect(xn - 1, yn - 1, 3, 3, kWhite);
}

// A dotted frame left free for daily notes: a label and three ruled lines (nothing is written in it yet).
template <class G>
void drawNotes(G& g) {
  const int x0 = 6, y0 = 138, x1 = W - 6, y1 = 196;
  for (int x = x0; x <= x1; x += 4) {
    g.drawPixel(x, y0, kBlack);
    g.drawPixel(x, y1, kBlack);
  }
  for (int y = y0; y <= y1; y += 4) {
    g.drawPixel(x0, y, kBlack);
    g.drawPixel(x1, y, kBlack);
  }
  drawStrip(g, ui::NOTES_LABEL, x1 - 5 - ui::NOTES_LABEL.w, y0 + 9 - ui::NOTES_LABEL.h / 2, kBlack);
  for (int k = 0; k < 3; k++)
    for (int x = x0 + 8; x < x1 - 7; x += 3) g.drawPixel(x, y0 + 27 + 13 * k, kBlack);
}

// Screen D: today. Weekday and date (Hebrew), the weather picture and highest/lowest, today's
// temperature graph, and the notes box. `now` is the local time. Without a clock there is no date and
// no graph; a forecast of another day is not shown (the next hourly fetch brings today's).
template <class G>
void drawWeather(G& g, const logic::WeatherDay& w, const Env& e, const tm& now) {
  g.fillScreen(kWhite);
  topBar(g, e);
  if (e.clockOk) {
    const Strip& wd = *ui::WEEKDAY[now.tm_wday % 7];
    drawStrip(g, wd, W - 5 - wd.w, 36 - wd.h / 2, kBlack);
    char n[4];
    snprintf(n, sizeof n, "%d", now.tm_mday);
    const int nw = textWidth(ui::WEEK, n);
    drawText(g, ui::WEEK, n, W - 5 - nw, 56, kBlack);  // right to left: the number, then the month
    const Strip& mo = *ui::MONTH[now.tm_mon % 12];
    drawStrip(g, mo, W - 5 - nw - 4 - mo.w, 56 - mo.h / 2, kBlack);
  }
  const uint32_t today = (uint32_t)((now.tm_year + 1900) * 10000 + (now.tm_mon + 1) * 100 + now.tm_mday);
  const bool have = e.clockOk && w.ymd != 0 && w.ymd == today;
  if (have) {
    const int hour = now.tm_hour % 24;
    const uint8_t kind = w.kind[hour] < 8 ? w.kind[hour] : 2;
    g.drawBitmap(4, 23, ui::WEATHER_ICON[kind], 44, 44, kBlack);
    int hi = w.temp[0], lo = w.temp[0];
    for (int i = 1; i < 24; i++) {
      if (w.temp[i] > hi) hi = w.temp[i];
      if (w.temp[i] < lo) lo = w.temp[i];
    }
    drawHiLo(g, hi, lo, 52, 36);
    const Strip& c = *ui::COND[kind];
    drawStrip(g, c, 52, 55 - c.h / 2, kBlack);
  }
  g.drawFastHLine(6, 70, W - 12, kBlack);
  if (have) drawGraph(g, w, now.tm_hour * 10 + now.tm_min / 6, 10, 74, W - 8, 134);
  drawNotes(g);
}

}  // namespace screens

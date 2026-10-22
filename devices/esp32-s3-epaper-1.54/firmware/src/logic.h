// Pure logic of the home display: no Arduino, no JSON library, so it also
// builds and is tested on the PC (test/run.sh).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace logic {

constexpr int kTop = 10;

// What the screens need to know about one species, from data.json.
struct Bird {
  char sci[32];
  char name[32];       // English name (shown when the species is not in the pack)
  char badge[3][16];   // first three badge names ("Migrant", ...)
  uint32_t seen;       // last_seen as an epoch, in the device's local time zone
  uint16_t week;       // week_count
};

inline void copyStr(char* dst, size_t n, const char* src) {
  if (!src) src = "";
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

// "Motacilla alba" -> "motacilla_alba": the card file name. Same rule as slug() in tools/build_pack.py.
inline void slugOf(const char* sci, char* out, size_t n) {
  size_t o = 0;
  bool underscore = false;
  for (const char* p = sci; *p && o + 1 < n; ++p) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') c += 32;
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out[o++] = c;
      underscore = false;
    } else if (o > 0 && !underscore) {
      out[o++] = '_';
      underscore = true;
    }
  }
  if (o > 0 && out[o - 1] == '_') o--;
  out[o] = 0;
}

// "2026-10-18T13:55:52" (no zone: the exporter's local clock) -> epoch in the current TZ.
inline bool parseLocal(const char* s, time_t* out) {
  int Y, M, D, h, m, sec = 0;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) < 5) return false;
  struct tm t = {};
  t.tm_year = Y - 1900;
  t.tm_mon = M - 1;
  t.tm_mday = D;
  t.tm_hour = h;
  t.tm_min = m;
  t.tm_sec = sec;
  t.tm_isdst = -1;
  time_t r = mktime(&t);
  if (r == (time_t)-1) return false;
  *out = r;
  return true;
}

// Keep `top` as the kTop birds seen most recently, newest first. Equal times keep
// arrival order. Feed it every row of data.json, one by one.
inline void insertTop(Bird* top, int& n, const Bird& b) {
  int pos = n;
  while (pos > 0 && top[pos - 1].seen < b.seen) pos--;
  if (pos >= kTop) return;
  int last = n < kTop ? n : kTop - 1;
  for (int i = last; i > pos; --i) top[i] = top[i - 1];
  top[pos] = b;
  if (n < kTop) n++;
}

// Which bird screen A shows after a refresh. A new detection (the newest bird
// changed) jumps to the front; otherwise a timer wake steps to the next of the
// latest 10 and any other wake keeps the current one.
inline int nextIndex(const Bird* oldTop, int oldN, const Bird* newTop, int newN, int oldIdx, bool advance) {
  if (newN <= 0) return 0;
  if (oldN <= 0 || oldTop[0].seen != newTop[0].seen || strcmp(oldTop[0].sci, newTop[0].sci) != 0) return 0;
  if (oldIdx < 0 || oldIdx >= newN) return 0;
  return advance ? (oldIdx + 1) % newN : oldIdx;
}

// The site marks species it is unsure about with the badge "Low confidence" (icon ?); its
// "Confident" filter hides them, and so does the display.
inline bool isLowConfidenceBadge(const char* badgeEn) { return strcmp(badgeEn, "Low confidence") == 0; }

// Rough LiPo state of charge from the resting voltage (piecewise linear). -1 = no battery
// (nothing connected or a nonsense reading).
inline int lipoPercent(float v) {
  if (v < 2.5f || v > 4.6f) return -1;
  static const float kV[] = {3.30f, 3.50f, 3.65f, 3.75f, 3.85f, 4.00f, 4.15f};
  static const int kP[] = {0, 10, 30, 50, 70, 85, 100};
  if (v <= kV[0]) return 0;
  for (int i = 1; i < 7; i++)
    if (v <= kV[i]) return kP[i - 1] + (int)((v - kV[i - 1]) / (kV[i] - kV[i - 1]) * (kP[i] - kP[i - 1]) + 0.5f);
  return 100;
}

enum class Heard { None, Once, Twice, Many };
inline Heard heardKind(unsigned week) {
  return week == 0 ? Heard::None : week == 1 ? Heard::Once : week == 2 ? Heard::Twice : Heard::Many;
}

enum class AgoKind { Min, Hour, Hours, Day };
struct Ago {
  AgoKind kind;
  int value;
};
// "7 min ago" / "an hour ago" / "N hours ago" / "yesterday" (the same thresholds as tools/mockups.py).
inline Ago ago(time_t now, time_t then) {
  long m = (long)((now - then) / 60);
  if (m < 60) return {AgoKind::Min, m < 1 ? 1 : (int)m};
  if (m < 120) return {AgoKind::Hour, 1};
  if (m < 24 * 60) return {AgoKind::Hours, (int)(m / 60)};
  return {AgoKind::Day, 1};
}

}  // namespace logic

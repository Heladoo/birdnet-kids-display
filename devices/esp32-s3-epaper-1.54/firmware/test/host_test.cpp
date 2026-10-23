// PC test of logic.h and screens.h. Usage: host_test fixture.tsv cards_dir out_dir
// Renders screens A and D to out_dir/*.pbm for comparison with tools/mockups.py.
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "../src/logic.h"
#include "../src/screens.h"
#include "mock_gfx.h"

static int failures = 0;
#define CHECK(cond, ...)                      \
  do {                                        \
    if (!(cond)) {                            \
      failures++;                             \
      printf("FAIL %s:%d ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                    \
      printf("\n");                           \
    }                                         \
  } while (0)

static std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> v;
  size_t a = 0;
  for (;;) {
    size_t b = s.find(sep, a);
    if (b == std::string::npos) { v.push_back(s.substr(a)); break; }
    v.push_back(s.substr(a, b - a));
    a = b + 1;
  }
  return v;
}

static bool readFile(const std::string& path, uint8_t* buf, size_t cap, size_t* n) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  *n = fread(buf, 1, cap, f);
  fclose(f);
  return true;
}

int main(int argc, char** argv) {
  if (argc < 4) { puts("usage: host_test fixture.tsv cards_dir out_dir"); return 2; }
  std::string cards = argv[2], out = argv[3];
  setenv("TZ", "UTC", 1);
  tzset();

  // ---- load the fixture, feed every row through insertTop
  FILE* f = fopen(argv[1], "r");
  if (!f) { puts("no fixture"); return 2; }
  char line[1024];
  time_t now = 0;
  logic::Bird top[logic::kTop];
  int n = 0, rows = 0;
  std::vector<std::string> expectTop;
  while (fgets(line, sizeof line, f)) {
    std::string s(line);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    auto c = split(s, '\t');
    if (c[0] == "NOW") {
      CHECK(logic::parseLocal(c[1].c_str(), &now), "parse NOW");
    } else if (c[0] == "B") {
      logic::Bird b = {};
      logic::copyStr(b.sci, sizeof b.sci, c[1].c_str());
      logic::copyStr(b.name, sizeof b.name, c[2].c_str());
      time_t seen;
      CHECK(logic::parseLocal(c[3].c_str(), &seen), "parse %s", c[3].c_str());
      b.seen = (uint32_t)seen;
      b.week = (uint16_t)atoi(c[4].c_str());
      auto bd = split(c[5], '|');
      for (size_t i = 0; i < bd.size() && i < 3; i++) logic::copyStr(b.badge[i], sizeof b.badge[i], bd[i].c_str());
      char slug[40];
      logic::slugOf(b.sci, slug, sizeof slug);
      CHECK(c[6] == slug, "slug of %s: got %s want %s", b.sci, slug, c[6].c_str());
      logic::insertTop(top, n, b);
      rows++;
    } else if (c[0] == "T") {
      expectTop.push_back(c[1]);
    }
  }
  fclose(f);
  CHECK(rows > 100, "rows %d", rows);
  CHECK(n == logic::kTop && expectTop.size() == (size_t)logic::kTop, "top size %d", n);
  for (int i = 0; i < n && i < (int)expectTop.size(); i++)
    CHECK(expectTop[i] == top[i].sci, "top[%d] = %s, want %s", i, top[i].sci, expectTop[i].c_str());

  // ---- rotation
  logic::Bird fresh[logic::kTop];
  memcpy(fresh, top, sizeof top);
  CHECK(logic::nextIndex(top, n, fresh, n, 3, true) == 4, "advance");
  CHECK(logic::nextIndex(top, n, fresh, n, 9, true) == 0, "wrap");
  CHECK(logic::nextIndex(top, n, fresh, n, 3, false) == 3, "keep on manual refresh");
  fresh[0].seen += 60;
  CHECK(logic::nextIndex(top, n, fresh, n, 5, true) == 0, "new detection jumps to the front");
  CHECK(logic::nextIndex(top, 0, fresh, n, 5, true) == 0, "no previous list");

  // ---- phrases
  CHECK(logic::heardKind(0) == logic::Heard::None && logic::heardKind(1) == logic::Heard::Once &&
        logic::heardKind(2) == logic::Heard::Twice && logic::heardKind(3) == logic::Heard::Many, "heard");
  CHECK(logic::ago(1000, 1000).kind == logic::AgoKind::Min && logic::ago(1000, 1000).value == 1, "ago now");
  CHECK(logic::ago(1000 + 7 * 60, 1000).value == 7, "ago 7 min");
  CHECK(logic::ago(1000 + 59 * 60, 1000).kind == logic::AgoKind::Min, "ago 59 min");
  CHECK(logic::ago(1000 + 60 * 60, 1000).kind == logic::AgoKind::Hour, "ago 1 h");
  CHECK(logic::ago(1000 + 3 * 3600 + 5, 1000).kind == logic::AgoKind::Hours && logic::ago(1000 + 3 * 3600 + 5, 1000).value == 3, "ago 3 h");
  CHECK(logic::ago(1000 + 24 * 3600, 1000).kind == logic::AgoKind::Day, "ago day");
  CHECK(logic::ago(1000, 2000).kind == logic::AgoKind::Min && logic::ago(1000, 2000).value == 1, "clock skew");

  // ---- cards
  static uint8_t buf[screens::kCardMax];
  size_t sz = 0;
  size_t biggest = 0;
  int loaded = 0;
  auto loader = [&](const char* sci, screens::Card& card) {
    char slug[40], path[256];
    logic::slugOf(sci, slug, sizeof slug);
    snprintf(path, sizeof path, "%s/%s.bin", cards.c_str(), slug);
    if (!readFile(path, buf, sizeof buf, &sz)) return false;
    if (sz > biggest) biggest = sz;
    loaded++;
    return screens::parseCard(buf, sz, card);
  };
  screens::Card card;
  CHECK(loader(top[0].sci, card), "card for %s", top[0].sci);
  CHECK(!loader("Nonexistent bird", card), "unknown species has no card");
  CHECK(!screens::parseCard((const uint8_t*)"XXXXXXXXXXXX", 12, card), "bad magic");
  CHECK(!screens::parseCard(buf, 10, card), "truncated card");

  // ---- render
  screens::Env env;
  env.sensorOk = true; env.temp = 23.4f; env.hum = 61;
  env.clockOk = true; env.hh = 14; env.mm = 5;
  static MockGfx g;
  CHECK(loader(top[0].sci, card), "card again");
  screens::drawBird(g, &card, top[0], now, env);
  CHECK(g.writePbm((out + "/a-bird.pbm").c_str()), "write A");
  // screen D, today's weather, from a synthetic forecast (a partly cloudy day with an evening shower)
  CHECK(logic::weatherKind(0, true) == logic::kSunny && logic::weatherKind(0, false) == logic::kNight &&
        logic::weatherKind(2, true) == logic::kPartly && logic::weatherKind(3, true) == logic::kCloudy &&
        logic::weatherKind(63, true) == logic::kRain && logic::weatherKind(95, true) == logic::kStorm &&
        logic::weatherKind(45, true) == logic::kFog && logic::weatherKind(71, true) == logic::kCloudy, "weather kinds");
  logic::WeatherDay wd = {};
  static const int8_t kT[24] = {19, 18, 18, 17, 17, 17, 18, 19, 21, 23, 25, 26, 27, 27, 27, 26, 25, 23, 22, 21, 20, 20, 19, 19};
  for (int i = 0; i < 24; i++) {
    wd.temp[i] = kT[i];
    wd.rain[i] = i >= 18 && i < 23 ? 10 * (i - 17) : 0;
    wd.kind[i] = logic::kPartly;
  }
  wd.ymd = 20261023;
  tm lt = {};
  lt.tm_year = 126; lt.tm_mon = 9; lt.tm_mday = 23; lt.tm_wday = 5; lt.tm_hour = 14; lt.tm_min = 20;
  screens::drawWeather(g, wd, env, lt);
  CHECK(g.writePbm((out + "/d-weather.pbm").c_str()), "write D");
  wd.ymd = 20261022;  // yesterday's forecast: no picture and no graph, only the date and the notes box
  screens::drawWeather(g, wd, env, lt);
  CHECK(g.writePbm((out + "/d-weather-stale.pbm").c_str()), "write D stale");
  // sensor/clock missing, "many" and "once" phrases, an hours-ago and a day-ago line
  env.sensorOk = false; env.clockOk = false;
  logic::Bird odd = top[1];
  odd.week = 1; odd.seen = (uint32_t)(now - 3 * 3600 - 60);
  loader(odd.sci, card);
  screens::drawBird(g, &card, odd, now, env);
  g.writePbm((out + "/a-once-hours-nodata.pbm").c_str());
  odd.week = 0; odd.seen = (uint32_t)(now - 30 * 3600);
  screens::drawBird(g, &card, odd, now, env);
  g.writePbm((out + "/a-none-day.pbm").c_str());

  printf("%d rows, top-10 ok=%d, largest card %zu B, %d failure(s)\n", rows, n == logic::kTop, biggest, failures);
  return failures ? 1 : 0;
}

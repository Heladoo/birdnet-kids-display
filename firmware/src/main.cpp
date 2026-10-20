// BirdNET Kids home display (PLAN.md section 7).
//
// Every wake: read the sensor and clock, maybe fetch data.json, draw screen A or D, sleep.
//   timer (5 min)   fetch, step A to the next of the latest 10 (a new detection jumps to the front)
//   BOOT            switch A <-> D, no Wi-Fi
//   PWR (or BOOT held 1.5 s if PWR is not a GPIO)   refresh now: fetch, redraw
//   power-on        fetch, redraw
// After a button wake it stays up for a few seconds so presses can follow each other.
// The speakers are never used: the amplifier pin is held off.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <sys/time.h>

#include "board_pins.h"
#include "soc/usb_serial_jtag_reg.h"
#include "logic.h"
#include "screens.h"
#ifndef DEMO_MODE
#include "secrets.h"
#endif

#ifndef PINS_PLACEHOLDER
static_assert(PIN_EPD_SCK >= 0 && PIN_EPD_MOSI >= 0 && PIN_EPD_CS >= 0 && PIN_EPD_DC >= 0 &&
                  PIN_EPD_RST >= 0 && PIN_EPD_BUSY >= 0 && PIN_I2C_SDA >= 0 && PIN_I2C_SCL >= 0,
              "Pins unconfirmed: fill include/board_pins.h from Waveshare's demo (PLAN.md section 2)");
#endif

#ifdef DEBUG_LOG
#define LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LOG(...) ((void)0)
#endif

static const char* kDataUrl = "https://raw.githubusercontent.com/Heladoo/Birdnet_kids/main/data.json";
static const char* kTz = "IST-2IDT,M3.4.4/26,M10.5.0";  // Asia/Jerusalem
constexpr uint32_t kRefreshMs = 5UL * 60 * 1000;
constexpr uint32_t kNtpEverySec = 24UL * 3600;
constexpr uint32_t kGraceMs = 4000;      // stay awake after a button wake, for the next press
constexpr uint32_t kHoldRefreshMs = 1500;
constexpr uint32_t kRtcMagic = 0xB1D0C0DE;
constexpr uint8_t kSht = 0x70, kPcf = 0x51;
constexpr int kPressed = LOW;  // buttons are active low

// A GxEPD2 class for the SSD1681 panel; confirm against Waveshare's demo in phase 0.
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

// Survives deep sleep.
struct RtcState {
  uint32_t magic;
  uint8_t screen;  // 0 = A (the bird), 1 = D (the latest 10)
  int8_t idx;      // which of the latest 10 screen A shows
  uint8_t n;
  uint32_t lastNtp;
  char etag[72];
  logic::Bird top[logic::kTop];
};
RTC_DATA_ATTR static RtcState st;

static screens::Env env;
static uint8_t cardBuf[screens::kCardMax];

// ------------------------------------------------------------------ sensor

static uint8_t crc8(const uint8_t* d, int n) {
  uint8_t c = 0xFF;
  for (int i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = (c & 0x80) ? (c << 1) ^ 0x31 : (c << 1);
  }
  return c;
}

static bool shtCmd(uint16_t c) {
  Wire.beginTransmission(kSht);
  Wire.write(c >> 8);
  Wire.write(c & 0xFF);
  return Wire.endTransmission() == 0;
}

// SHTC3: wake, one measurement (temperature first, no clock stretching), sleep.
static bool readSht(float& t, float& h) {
  if (!shtCmd(0x3517)) return false;
  delay(1);
  bool ok = shtCmd(0x7866);
  if (ok) {
    delay(15);
    uint8_t b[6];
    ok = Wire.requestFrom((uint16_t)kSht, (size_t)6, true) == 6;
    for (int i = 0; ok && i < 6; i++) b[i] = Wire.read();
    ok = ok && crc8(b, 2) == b[2] && crc8(b + 3, 2) == b[5];
    if (ok) {
      t = -45.0f + 175.0f * (uint16_t)((b[0] << 8) | b[1]) / 65535.0f;
      h = 100.0f * (uint16_t)((b[3] << 8) | b[4]) / 65535.0f;
    }
  }
  shtCmd(0xB098);  // sleep
  return ok;
}

static void readBattery() {
  analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) {
    mv += analogReadMilliVolts(PIN_BAT_ADC);
    delay(2);
  }
  float v = mv / 8.0f * 2.0f / 1000.0f;  // 1:2 divider
  env.batPct = logic::lipoPercent(v);
  LOG("battery: %.2f V -> %d %%\n", v, env.batPct);
}

// ------------------------------------------------------------------- clock

static uint8_t bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t toBcd(int v) { return ((v / 10) << 4) | (v % 10); }

// PCF85063: registers 0x04..0x0A hold sec, min, hour, day, weekday, month, year (BCD, local time).
static bool pcfRead(tm& t) {
  Wire.beginTransmission(kPcf);
  Wire.write(0x04);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint16_t)kPcf, (size_t)7, true) != 7) return false;
  uint8_t r[7];
  for (int i = 0; i < 7; i++) r[i] = Wire.read();
  if (r[0] & 0x80) return false;  // oscillator stopped: the time is not valid
  t = {};
  t.tm_sec = bcd(r[0] & 0x7F);
  t.tm_min = bcd(r[1] & 0x7F);
  t.tm_hour = bcd(r[2] & 0x3F);
  t.tm_mday = bcd(r[3] & 0x3F);
  t.tm_mon = bcd(r[5] & 0x1F) - 1;
  t.tm_year = 100 + bcd(r[6]);
  t.tm_isdst = -1;
  return t.tm_year >= 124;  // 2024 or later; a never-set chip says 2000
}

static void pcfWrite(const tm& t) {
  Wire.beginTransmission(kPcf);
  Wire.write(0x04);
  Wire.write(toBcd(t.tm_sec));  // bit 7 clear: this also clears the oscillator-stopped flag
  Wire.write(toBcd(t.tm_min));
  Wire.write(toBcd(t.tm_hour));
  Wire.write(toBcd(t.tm_mday));
  Wire.write(t.tm_wday);
  Wire.write(toBcd(t.tm_mon + 1));
  Wire.write(toBcd(t.tm_year % 100));
  Wire.endTransmission();
}

static bool loadClockFromRtc() {
  setenv("TZ", kTz, 1);
  tzset();
  tm t;
  if (!pcfRead(t)) return false;
  timeval tv = {mktime(&t), 0};
  settimeofday(&tv, nullptr);
  return true;
}

// ------------------------------------------------------------------- Wi-Fi

static bool wifiUp() {
#ifdef DEMO_MODE
  return false;
#else
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (uint32_t t0 = millis(); millis() - t0 < 12000;) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(100);
  }
  return false;
#endif
}

static void wifiDown() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// NTP -> system clock and PCF85063.
static bool syncNtp() {
  configTzTime(kTz, "pool.ntp.org", "time.google.com");
  for (uint32_t t0 = millis(); millis() - t0 < 8000;) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      time_t now = time(nullptr);
      tm lt;
      localtime_r(&now, &lt);
      pcfWrite(lt);
      st.lastNtp = (uint32_t)now;
      return true;
    }
    delay(100);
  }
  return false;
}

// ArduinoJson takes any "reader" with read() and readBytes(). A plain TLS stream returns -1
// whenever the next packet has not arrived yet, which the parser takes as the end of the
// file (IncompleteInput, seen after 3 entries). This one waits for data until the server
// closes the connection, or nothing arrives for 10 s.
struct PatientReader {
  WiFiClient* c;
  size_t readBytes(char* buf, size_t n) {
    size_t got = 0;
    uint32_t last = millis();
    while (got < n) {
      int avail = c->available();
      if (avail > 0) {
        int r = c->read((uint8_t*)buf + got, (size_t)avail < n - got ? (size_t)avail : n - got);
        if (r > 0) {
          got += r;
          last = millis();
          continue;
        }
      }
      if (!c->connected() && c->available() == 0) break;
      if (millis() - last > 10000) break;
      delay(1);
    }
    return got;
  }
  int read() {
    char b;
    return readBytes(&b, 1) == 1 ? (uint8_t)b : -1;
  }
};

enum class Fetch { Failed, Same, Updated };

// GET data.json (~250 KB), keeping only what the screens need, and build the latest 10.
// If-None-Match makes an unchanged file a tiny 304.
static Fetch fetchTop(logic::Bird* out, int& n) {
  // The data is public and only drives a display, so the TLS certificate is not checked.
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding: the JSON parser reads the stream directly
  http.setTimeout(15000);
  if (!http.begin(client, kDataUrl)) { LOG("fetch: http.begin failed\n"); return Fetch::Failed; }
  const char* keys[] = {"ETag"};
  http.collectHeaders(keys, 1);
  if (st.etag[0]) http.addHeader("If-None-Match", st.etag);
  int code = http.GET();
  LOG("fetch: HTTP %d (%s), heap %u\n", code, code < 0 ? HTTPClient::errorToString(code).c_str() : "", (unsigned)ESP.getFreeHeap());
  if (code == 304) {
    http.end();
    return Fetch::Same;
  }
  if (code != 200) {
    http.end();
    return Fetch::Failed;
  }
  String etag = http.header("ETag");

  JsonDocument filter;
  filter[0]["sci"] = true;
  filter[0]["name"] = true;
  filter[0]["last_seen"] = true;
  filter[0]["week_count"] = true;
  filter[0]["badges"][0]["en"] = true;
  JsonDocument doc;
  PatientReader reader{http.getStreamPtr()};
  DeserializationError err = deserializeJson(doc, reader, DeserializationOption::Filter(filter));
  http.end();
  LOG("fetch: parse %s, %u entries\n", err.c_str(), (unsigned)doc.as<JsonArray>().size());
  if (err) return Fetch::Failed;

  n = 0;
  for (JsonObject o : doc.as<JsonArray>()) {
    logic::Bird b = {};
    time_t seen;
    if (!logic::parseLocal(o["last_seen"] | "", &seen)) { LOG("fetch: bad last_seen '%s'\n", (const char*)(o["last_seen"] | "")); continue; }
    logic::copyStr(b.sci, sizeof b.sci, o["sci"] | "");
    logic::copyStr(b.name, sizeof b.name, o["name"] | "");
    b.seen = (uint32_t)seen;
    b.week = o["week_count"] | 0;
    int k = 0;
    for (JsonObject bd : o["badges"].as<JsonArray>()) {
      if (k >= 3) break;
      logic::copyStr(b.badge[k++], sizeof b.badge[0], bd["en"] | "");
    }
    logic::insertTop(out, n, b);
  }
  LOG("fetch: %d birds kept\n", n);
  if (n == 0) return Fetch::Failed;
  logic::copyStr(st.etag, sizeof st.etag, etag.c_str());
  return Fetch::Updated;
}

static void nextBird() { st.idx = st.n ? (st.idx + 1) % st.n : 0; }

// Fetch and rotate. `advance` = step A to the next bird (timer wakes only).
static void refreshData(bool advance) {
  bool wifi = wifiUp();
  LOG("wifi: %s\n", wifi ? "connected" : "FAILED");
  if (wifi) {
    if (!env.clockOk || st.lastNtp == 0 || (uint32_t)time(nullptr) - st.lastNtp > kNtpEverySec) {
      if (syncNtp()) env.clockOk = true;
    }
    logic::Bird fresh[logic::kTop];
    int n = 0;
    Fetch r = fetchTop(fresh, n);
    if (r == Fetch::Updated) {
      st.idx = logic::nextIndex(st.top, st.n, fresh, n, st.idx, advance);
      memcpy(st.top, fresh, sizeof st.top);
      st.n = n;
    } else if (r == Fetch::Same) {
      st.idx = logic::nextIndex(st.top, st.n, st.top, st.n, st.idx, advance);
    }
    wifiDown();
  }
  // No Wi-Fi: draw the last list again (the "ago" line still moves with the clock).
}

// ------------------------------------------------------------------ drawing

static bool loadCard(const char* sci, screens::Card& c) {
  char slug[40], path[64];
  logic::slugOf(sci, slug, sizeof slug);
  snprintf(path, sizeof path, "/cards/%s.bin", slug);
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  size_t n = f.read(cardBuf, sizeof cardBuf);
  f.close();
  return screens::parseCard(cardBuf, n, c);
}

// Full refresh, every time: partial refresh after deep sleep is untested on this panel
// (see PLAN.md section 1); a full refresh takes ~2 s and also clears ghosting.
static void render() {
  time_t now = time(nullptr);
  tm lt;
  localtime_r(&now, &lt);
  env.hh = lt.tm_hour;
  env.mm = lt.tm_min;
  display.setFullWindow();
  display.firstPage();
  do {
    if (st.n == 0) {
      display.fillScreen(GxEPD_WHITE);
      screens::topBar(display, env);
    } else if (st.screen == 0) {
      screens::Card card;
      const logic::Bird& b = st.top[st.idx < st.n ? st.idx : 0];
      bool have = loadCard(b.sci, card);
      screens::drawBird(display, have ? &card : nullptr, b, now, env);
    } else {
      screens::drawList(display, st.top, st.n, st.idx, env, loadCard);
    }
  } while (display.nextPage());
  display.hibernate();
}

// -------------------------------------------------------------------- misc

static bool pressed(int pin) { return pin >= 0 && digitalRead(pin) == kPressed; }

static void waitReleased(uint32_t maxMs) {
  for (uint32_t t0 = millis(); millis() - t0 < maxMs && (pressed(PIN_BTN_BOOT) || pressed(PIN_BTN_PWR));) delay(10);
}

enum class Btn { None, Boot, Pwr };

static Btn wakeButton() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) return Btn::None;
  uint64_t m = esp_sleep_get_ext1_wakeup_status();
  if (PIN_BTN_BOOT >= 0 && (m & (1ULL << PIN_BTN_BOOT))) return Btn::Boot;
  if (PIN_BTN_PWR >= 0 && (m & (1ULL << PIN_BTN_PWR))) return Btn::Pwr;
  return Btn::None;
}

static void holdPinsForSleep() {
  if (PIN_PWR_LATCH >= 0) gpio_hold_en((gpio_num_t)PIN_PWR_LATCH);
  if (PIN_AMP_EN >= 0) gpio_hold_en((gpio_num_t)PIN_AMP_EN);
  if (PIN_EPD_PWR >= 0) gpio_hold_en((gpio_num_t)PIN_EPD_PWR);
  gpio_deep_sleep_hold_en();
}

// A PC on the USB port sends a start-of-frame packet every millisecond; a charger does not.
// Deep sleep switches the USB port off (Windows sees an unplug), so with a PC attached the
// device stays awake instead: buttons answer at once and nothing disconnects.
static bool usbHostPresent() {
  uint32_t a = REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG) & 0x7FF;
  delay(4);
  uint32_t b = REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG) & 0x7FF;
  return a != b;
}

// Returns when the PC is unplugged (then the caller goes to deep sleep as usual).
static void stayAwakeOnUsb() {
  uint32_t lastData = millis(), lastCheck = millis();
  int absent = 0;
  for (;;) {
    bool boot = pressed(PIN_BTN_BOOT), pwr = pressed(PIN_BTN_PWR);
    if (boot || pwr) {
      uint32_t t0 = millis();
      while ((pressed(PIN_BTN_BOOT) || pressed(PIN_BTN_PWR)) && millis() - t0 <= kHoldRefreshMs) delay(10);
      bool held = millis() - t0 > kHoldRefreshMs;
      env.sensorOk = readSht(env.temp, env.hum);
      readBattery();
      if (pwr && held) {
        refreshData(false);
        lastData = millis();
      } else if (pwr) {
        nextBird();
      } else {
        st.screen ^= 1;
      }
      render();
      waitReleased(3000);
    } else if (millis() - lastData >= kRefreshMs) {
      env.sensorOk = readSht(env.temp, env.hum);
      readBattery();
      refreshData(true);
      lastData = millis();
      render();
    }
    if (millis() - lastCheck >= 2000) {
      lastCheck = millis();
      absent = usbHostPresent() ? 0 : absent + 1;
      if (absent >= 2) return;
    }
    delay(20);
  }
}

static void goToSleep() {
#ifdef DEBUG_LOG
  LOG("done: n=%d idx=%d screen=%d (debug build stays awake)\n", st.n, st.idx, st.screen);
  return;
#endif
  waitReleased(2000);
  if (usbHostPresent()) stayAwakeOnUsb();
  uint64_t mask = 0;
  for (int pin : {PIN_BTN_BOOT, PIN_BTN_PWR}) {
    if (pin < 0) continue;
    rtc_gpio_pullup_en((gpio_num_t)pin);
    rtc_gpio_pulldown_dis((gpio_num_t)pin);
    mask |= 1ULL << pin;
  }
  if (mask) esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  uint32_t up = millis();
  esp_sleep_enable_timer_wakeup((uint64_t)(kRefreshMs > up + 1000 ? kRefreshMs - up : 1000) * 1000ULL);
  holdPinsForSleep();
  esp_deep_sleep_start();
}

static void initPins() {
  if (PIN_PWR_LATCH >= 0) {
    gpio_hold_dis((gpio_num_t)PIN_PWR_LATCH);
    pinMode(PIN_PWR_LATCH, OUTPUT);
    digitalWrite(PIN_PWR_LATCH, PWR_LATCH_ON);
  }
  if (PIN_AMP_EN >= 0) {
    gpio_hold_dis((gpio_num_t)PIN_AMP_EN);
    pinMode(PIN_AMP_EN, OUTPUT);
    digitalWrite(PIN_AMP_EN, AMP_OFF);
  }
  if (PIN_EPD_PWR >= 0) {
    gpio_hold_dis((gpio_num_t)PIN_EPD_PWR);
    pinMode(PIN_EPD_PWR, OUTPUT);
    digitalWrite(PIN_EPD_PWR, EPD_PWR_ON);
  }
  for (int pin : {PIN_BTN_BOOT, PIN_BTN_PWR}) {
    if (pin >= 0) {
      rtc_gpio_deinit((gpio_num_t)pin);
      pinMode(pin, INPUT_PULLUP);
    }
  }
}

#ifdef DEMO_MODE
// Fixed data so screens can be checked against tools/mockups.py without Wi-Fi or a pack of photos.
// Serial: a = screen A, d = screen D, n = next bird.
static void demoData() {
  setenv("TZ", kTz, 1);
  tzset();
  timeval tv = {1792332300, 0};  // 2026-10-18 14:05 local
  settimeofday(&tv, nullptr);
  env.clockOk = true;
  env.sensorOk = true;
  env.temp = 23.4f;
  env.hum = 61;
  static const char* const sci[] = {"Motacilla alba", "Psittacula krameri", "Otus scops"};
  static const char* const name[] = {"White Wagtail", "Rose-ringed Parakeet", "Eurasian Scops Owl"};
  static const uint16_t week[] = {284, 1, 0};
  st.n = 3;
  for (int i = 0; i < 3; i++) {
    st.top[i] = {};
    logic::copyStr(st.top[i].sci, sizeof st.top[i].sci, sci[i]);
    logic::copyStr(st.top[i].name, sizeof st.top[i].name, name[i]);
    logic::copyStr(st.top[i].badge[0], sizeof st.top[i].badge[0], i == 0 ? "Migrant" : "Resident");
    st.top[i].seen = (uint32_t)tv.tv_sec - 600 - i * 3000;
    st.top[i].week = week[i];
  }
}
#endif

// -------------------------------------------------------------------- main

void setup() {
#ifdef DEBUG_LOG
  Serial.begin(115200);
  delay(4000);  // time for the PC to attach to the USB serial port
  LOG("debug build, reset reason %d\n", (int)esp_reset_reason());
#endif
  const bool coldStart = st.magic != kRtcMagic;
  if (coldStart) {
    memset(&st, 0, sizeof st);
    st.magic = kRtcMagic;
  }
  initPins();
  Btn btn = wakeButton();
  auto cause = esp_sleep_get_wakeup_cause();

  LittleFS.begin(false);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  env.sensorOk = readSht(env.temp, env.hum);  // before Wi-Fi warms the board
  readBattery();
  env.clockOk = loadClockFromRtc();
  SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);
  display.epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(0, coldStart, 2, false);

#ifdef DEMO_MODE
  Serial.begin(115200);
  demoData();
  render();
  Serial.println("demo: a / d / n");
  return;
#endif

  // PWR: short = next bird (from memory, no Wi-Fi: fast), held 1.5 s = refresh now (Wi-Fi).
  // BOOT held 1.5 s = "refresh now" when PWR is not a readable GPIO.
  bool refreshNow = false, nextBirdNow = false;
  if (btn == Btn::Pwr) {
    for (uint32_t t0 = millis(); pressed(PIN_BTN_PWR) && !refreshNow; delay(10))
      if (millis() - t0 >= kHoldRefreshMs) refreshNow = true;
    nextBirdNow = !refreshNow;
  } else if (btn == Btn::Boot && PIN_BTN_PWR < 0) {
    for (uint32_t t0 = millis(); pressed(PIN_BTN_BOOT) && !refreshNow; delay(10))
      if (millis() - t0 >= kHoldRefreshMs) refreshNow = true;
  }

  if (btn == Btn::Boot && !refreshNow) {
    st.screen ^= 1;
  } else if (nextBirdNow) {
    nextBird();
  } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    refreshData(true);
  } else {
    refreshData(false);  // power-on, reset, or PWR
  }
  render();

  if (btn != Btn::None) {  // a person is here: wait for the next press
    waitReleased(3000);     // a press still held from the wake-up is not a new one
    for (uint32_t last = millis(); millis() - last < kGraceMs; delay(10)) {
      bool boot = pressed(PIN_BTN_BOOT), pwr = pressed(PIN_BTN_PWR);
      if (!boot && !pwr) continue;
      uint32_t t0 = millis();
      while (pressed(PIN_BTN_BOOT) || pressed(PIN_BTN_PWR)) {
        delay(10);
        if (millis() - t0 > kHoldRefreshMs) break;
      }
      bool held = millis() - t0 > kHoldRefreshMs;
      if ((pwr && held) || (boot && PIN_BTN_PWR < 0 && held)) {
        refreshData(false);
      } else if (pwr) {
        nextBird();
      } else {
        st.screen ^= 1;
      }
      render();
      waitReleased(3000);
      last = millis();
    }
  }
  goToSleep();
}

void loop() {
#ifdef DEMO_MODE
  // Buttons, for bring-up: BOOT = switch A/D (as in the real firmware), PWR = next bird.
  if (pressed(PIN_BTN_BOOT) || pressed(PIN_BTN_PWR)) {
    delay(30);  // debounce
    const bool boot = pressed(PIN_BTN_BOOT), pwr = pressed(PIN_BTN_PWR);
    if (!boot && !pwr) return;
    Serial.printf("demo: button %s\n", boot ? "BOOT" : "PWR");
    if (boot) st.screen ^= 1;
    else st.idx = (st.idx + 1) % st.n;
    render();
    waitReleased(3000);
    return;
  }
  if (!Serial.available()) return;
  switch (Serial.read()) {
    case 'a': st.screen = 0; break;
    case 'd': st.screen = 1; break;
    case 'n': st.idx = (st.idx + 1) % st.n; break;
    case 'p': Serial.printf("demo: BOOT(gpio%d)=%d PWR(gpio%d)=%d\n", PIN_BTN_BOOT, digitalRead(PIN_BTN_BOOT), PIN_BTN_PWR, digitalRead(PIN_BTN_PWR)); return;
    default: return;
  }
  render();
#endif
}

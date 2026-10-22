// Board diagnostics: stress-tests the SHTC3 temperature sensor under different power conditions and
// reads the battery voltage, to find out why the display sometimes shows "--" and what can tell
// "plugged in" from "on battery". Build: `pio run -e board_diag -t upload`, open the serial monitor.
// Runs once at boot; send 'r' to run again.
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include "board_pins.h"
#include "soc/usb_serial_jtag_reg.h"

constexpr uint8_t kSht = 0x70;

static void epdPower(bool on) { digitalWrite(PIN_EPD_PWR, on ? EPD_PWR_ON : !EPD_PWR_ON); }
static void audioPower(bool on) { digitalWrite(PIN_AMP_EN, on ? !AMP_OFF : AMP_OFF); }

static bool cmd(uint16_t c, uint8_t* err) {
  Wire.beginTransmission(kSht);
  Wire.write(c >> 8);
  Wire.write(c & 0xFF);
  *err = Wire.endTransmission();  // 0 ok, 2 NACK on address, 3 NACK on data, 4 other, 5 timeout
  return *err == 0;
}
static uint8_t crc8(const uint8_t* d, int n) {
  uint8_t c = 0xFF;
  for (int i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = (c & 0x80) ? (c << 1) ^ 0x31 : (c << 1);
  }
  return c;
}

// Same sequence as the firmware's readSht. Returns 0 ok, else the failing stage:
// 1 wake, 2 measure, 3 short read, 4 crc.
static int shtRead(float& t, float& h, uint8_t* err) {
  *err = 0;
  if (!cmd(0x3517, err)) return 1;
  delay(1);
  if (!cmd(0x7866, err)) {
    cmd(0xB098, err);
    return 2;
  }
  delay(15);
  uint8_t b[6];
  bool ok = Wire.requestFrom((uint16_t)kSht, (size_t)6, true) == 6;
  for (int i = 0; ok && i < 6; i++) b[i] = Wire.read();
  uint8_t e2;
  cmd(0xB098, &e2);
  if (!ok) return 3;
  if (crc8(b, 2) != b[2] || crc8(b + 3, 2) != b[5]) return 4;
  t = -45.0f + 175.0f * (uint16_t)((b[0] << 8) | b[1]) / 65535.0f;
  h = 100.0f * (uint16_t)((b[3] << 8) | b[4]) / 65535.0f;
  return 0;
}

static void run(const char* label, int n, int gapMs, int preDelayMs = 0, bool cycleEpd = false) {
  int ok = 0, stage[5] = {}, nack2 = 0, nack3 = 0, nack5 = 0;
  float t = 0, h = 0;
  for (int i = 0; i < n; i++) {
    if (cycleEpd) {
      epdPower(false);
      delay(300);
      epdPower(true);
    }
    if (preDelayMs) delay(preDelayMs);
    uint8_t err;
    int r = shtRead(t, h, &err);
    stage[r]++;
    if (r == 0) ok++;
    if (err == 2) nack2++;
    if (err == 3) nack3++;
    if (err == 5) nack5++;
    delay(gapMs);
  }
  Serial.printf("%-46s ok %3d/%d | fail wake %d measure %d short %d crc %d | i2c err2 %d err3 %d err5 %d | last %.1f C %.0f %%\n",
                label, ok, n, stage[1], stage[2], stage[3], stage[4], nack2, nack3, nack5, t, h);
}

static float batteryVolts() {
  analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) {
    mv += analogReadMilliVolts(PIN_BAT_ADC);
    delay(2);
  }
  return mv / 16.0f * 2.0f / 1000.0f;
}

static bool usbHost() {
  uint32_t a = REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG) & 0x7FF;
  delay(4);
  uint32_t b = REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG) & 0x7FF;
  return a != b;
}

static void diagnose() {
  Serial.println("=== SHTC3 stress test (firmware conditions first)");
  epdPower(true);
  audioPower(false);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  delay(100);
  run("A  EPD rail on, audio rail off (as firmware)", 200, 30);
  audioPower(true);
  delay(100);
  run("B  EPD rail on, audio rail ON", 200, 30);
  audioPower(false);
  delay(50);
  run("C  as A again", 100, 30);
  epdPower(false);
  delay(100);
  run("D  EPD rail OFF", 30, 30);
  epdPower(true);
  delay(5);
  run("E  right after EPD rail on, +0 ms", 30, 0, 0, true);
  run("F  EPD rail cycled, +5 ms", 30, 0, 5, true);
  run("G  EPD rail cycled, +20 ms", 30, 0, 20, true);
  run("H  EPD rail cycled, +100 ms", 30, 0, 100, true);
  Serial.println("--- Wire.end()/begin() between reads (as the audio code does)");
  int ok = 0;
  for (int i = 0; i < 50; i++) {
    Wire.end();
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    float t, h;
    uint8_t err;
    if (shtRead(t, h, &err) == 0) ok++;
    delay(20);
  }
  Serial.printf("I  Wire re-init before each read: ok %d/50\n", ok);

  Serial.println("=== Battery / power");
  Serial.printf("USB host present (SOF): %s\n", usbHost() ? "yes" : "no");
  Serial.printf("VBAT idle: %.3f V\n", batteryVolts());
  Serial.print("VBAT during a Wi-Fi scan (radio load): ");
  WiFi.mode(WIFI_STA);
  WiFi.scanNetworks(true);
  float vmin = 9, vmax = 0;
  for (int i = 0; i < 60; i++) {
    float v = batteryVolts();
    if (v < vmin) vmin = v;
    if (v > vmax) vmax = v;
    if (WiFi.scanComplete() >= 0) break;
  }
  Serial.printf("min %.3f max %.3f V (swing %.0f mV)\n", vmin, vmax, (vmax - vmin) * 1000);
  WiFi.mode(WIFI_OFF);
  Serial.printf("VBAT after: %.3f V\n", batteryVolts());
  Serial.println("=== done ('r' to run again)");
}

void setup() {
  pinMode(PIN_EPD_PWR, OUTPUT);
  pinMode(PIN_AMP_EN, OUTPUT);
  pinMode(PIN_PWR_LATCH, OUTPUT);
  digitalWrite(PIN_PWR_LATCH, PWR_LATCH_ON);
  pinMode(PIN_PA_EN, OUTPUT);
  digitalWrite(PIN_PA_EN, LOW);
  Serial.begin(115200);
  delay(3000);
  Serial.println("board_diag");
  diagnose();
}

void loop() {
  while (Serial.available())
    if (Serial.read() == 'r') diagnose();
  delay(50);
}

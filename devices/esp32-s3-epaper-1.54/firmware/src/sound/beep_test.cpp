// Sound POC, stage 0: a generated tone through the ES8311 codec and the speaker.
// Nothing is stored or streamed; this proves wiring, I2S + MCLK, codec init, amplifier
// polarity and volume. Build: `pio run -e sound_beep -t upload`, then open the serial monitor.
//
// Trigger another round with the serial key 'b', the BOOT button or the PWR button.
// The first beeps are very quiet on purpose: the speaker is small and close.
#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>
#include <math.h>
#include "es8311_min.h"

// Waveshare ESP32-S3-ePaper-1.54 (see Waveshare's demo and the factory firmware log).
constexpr int kI2cSda = 47, kI2cScl = 48;
constexpr int kI2sMclk = 14, kI2sBclk = 15, kI2sWs = 38, kI2sDout = 45, kI2sDin = 16;
constexpr int kAudioPower = 42;  // codec supply, ACTIVE LOW
constexpr int kAmpEnable = 46;   // speaker amplifier, active high (strapping pin: keep low at boot)
constexpr int kBtnBoot = 0, kBtnPwr = 18;
constexpr uint32_t kRate = 16000;

static I2SClass i2s;

// 16-bit stereo sine, `ms` long, frequency `hz`, amplitude 0..32767.
static void tone(float hz, int ms, int amp) {
  constexpr int kFrames = 256;
  static int16_t buf[kFrames * 2];
  const int total = (int)((uint64_t)kRate * ms / 1000);
  const float twoPi = 2.0f * (float)M_PI;
  const float step = twoPi * hz / kRate;
  float phase = 0;
  for (int done = 0; done < total;) {
    int n = min(kFrames, total - done);
    for (int i = 0; i < n; i++) {
      // 5 ms fade in and out against clicks
      int pos = done + i, edge = min(pos, total - 1 - pos);
      float fade = edge < (int)(kRate / 200) ? (float)edge / (kRate / 200) : 1.0f;
      int16_t s = (int16_t)(sinf(phase) * amp * fade);
      phase += step;
      if (phase > twoPi) phase -= twoPi;
      buf[2 * i] = buf[2 * i + 1] = s;
    }
    i2s.write((const uint8_t*)buf, n * 4);
    done += n;
  }
}

static void silence(int ms) {
  static int16_t z[512] = {};
  for (int left = (int)((uint64_t)kRate * ms / 1000); left > 0; left -= 256) i2s.write((const uint8_t*)z, sizeof z);
}

static void playRound() {
  Serial.println("--- audio on");
  digitalWrite(kAudioPower, LOW);  // codec supply on
  delay(30);
  i2s.setPins(kI2sBclk, kI2sWs, kI2sDout, kI2sDin, kI2sMclk);
  if (!i2s.begin(I2S_MODE_STD, kRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("I2S begin FAILED");
    digitalWrite(kAudioPower, HIGH);
    return;
  }
  Serial.println("I2S running, MCLK on GPIO14");
  delay(20);

  Serial.printf("ES8311 ack: %s, chip id %02X %02X, version %02X\n", es8311::present() ? "yes" : "NO",
                es8311::read(0xFD), es8311::read(0xFE), es8311::read(0xFF));
  Serial.printf("open: %s\n", es8311::open() ? "ok" : "I2C WRITE ERRORS");
  Serial.printf("format 16 kHz: %s\n", es8311::setFormat(kRate) ? "ok" : "FAILED");
  es8311::mute(true);
  Serial.printf("start: %s\n", es8311::start() ? "ok" : "I2C WRITE ERRORS");
  es8311::setVolumeDb(-30);
  digitalWrite(kAmpEnable, HIGH);  // amplifier on, while the codec output is muted
  delay(20);
  es8311::mute(false);

  struct Beep {
    float hz, db;
    const char* what;
  };
  static const Beep beeps[] = {
      {440, -30, "440 Hz, -30 dB (very quiet)"},
      {440, -18, "440 Hz, -18 dB"},
      {440, -6, "440 Hz, -6 dB"},
      {880, -18, "880 Hz, -18 dB"},
  };
  for (const Beep& b : beeps) {
    Serial.printf("beep: %s\n", b.what);
    es8311::setVolumeDb(b.db);
    tone(b.hz, 500, 20000);
    silence(250);
  }

  es8311::mute(true);
  silence(50);
  digitalWrite(kAmpEnable, LOW);
  es8311::stop();
  i2s.end();
  digitalWrite(kAudioPower, HIGH);  // codec supply off
  Serial.println("--- audio off. Press 'b' / BOOT / PWR for another round.");
}

void setup() {
  // Everything off first: supply off (active low) and amplifier off.
  pinMode(kAudioPower, OUTPUT);
  digitalWrite(kAudioPower, HIGH);
  pinMode(kAmpEnable, OUTPUT);
  digitalWrite(kAmpEnable, LOW);
  pinMode(kBtnBoot, INPUT_PULLUP);
  pinMode(kBtnPwr, INPUT_PULLUP);
  Serial.begin(115200);
  delay(3000);
  Serial.println("sound_beep: stage 0 of the sound POC");
  Wire.begin(kI2cSda, kI2cScl);
  playRound();
}

void loop() {
  bool go = false;
  while (Serial.available()) go |= Serial.read() == 'b';
  if (digitalRead(kBtnBoot) == LOW || digitalRead(kBtnPwr) == LOW) {
    go = true;
    while (digitalRead(kBtnBoot) == LOW || digitalRead(kBtnPwr) == LOW) delay(10);
  }
  if (go) playRound();
  delay(20);
}

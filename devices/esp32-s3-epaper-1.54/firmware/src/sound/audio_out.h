// Speaker output of the Waveshare ESP32-S3-ePaper-1.54: codec supply, I2S, ES8311, amplifier.
// One clip at a time: begin(rate) .. write() .. end(). Everything is switched off again in end(),
// so nothing draws power (or hisses) between clips.
#pragma once
#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>
#include "es8311_min.h"

class AudioOut {
 public:
  static constexpr int kI2cSda = 47, kI2cScl = 48;
  static constexpr int kMclk = 14, kBclk = 15, kWs = 38, kDout = 45, kDin = 16;
  static constexpr int kSupplyPin = 42;  // codec supply, ACTIVE LOW
  static constexpr int kAmpPin = 46;     // speaker amplifier, active high (strapping pin: low at boot)

  // Call once at boot: everything off.
  void idle() {
    pinMode(kSupplyPin, OUTPUT);
    digitalWrite(kSupplyPin, HIGH);
    pinMode(kAmpPin, OUTPUT);
    digitalWrite(kAmpPin, LOW);
  }

  // Power up and configure for 16-bit stereo at `rate` (MCLK = 256 x rate). The sound starts
  // muted and is unmuted once the amplifier has settled.
  bool begin(uint32_t rate, float volumeDb) {
    if (open_) end();
    digitalWrite(kSupplyPin, LOW);
    delay(30);
    Wire.begin(kI2cSda, kI2cScl);
    i2s_.setPins(kBclk, kWs, kDout, kDin, kMclk);
    if (!i2s_.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
      digitalWrite(kSupplyPin, HIGH);
      return false;
    }
    delay(20);
    bool ok = es8311::present() && es8311::open() && es8311::setFormat(rate);
    es8311::mute(true);
    ok = ok && es8311::start();
    es8311::setVolumeDb(volumeDb);
    digitalWrite(kAmpPin, HIGH);
    delay(20);
    es8311::mute(false);
    open_ = true;
    rate_ = rate;
    if (!ok) end();
    return ok;
  }

  // Interleaved 16-bit stereo frames; blocks until the I2S DMA has room (that paces playback).
  void write(const int16_t* stereo, size_t frames) { i2s_.write((const uint8_t*)stereo, frames * 4); }

  void end() {
    if (!open_) return;
    es8311::mute(true);
    delay(30);
    digitalWrite(kAmpPin, LOW);
    es8311::stop();
    i2s_.end();
    digitalWrite(kSupplyPin, HIGH);
    open_ = false;
  }

  bool isOpen() const { return open_; }
  uint32_t rate() const { return rate_; }

 private:
  I2SClass i2s_;
  bool open_ = false;
  uint32_t rate_ = 0;
};

// Minimal ES8311 playback driver (DAC only, I2S slave, MCLK = 256 x sample rate).
//
// The register sequence and clock coefficients are taken from Espressif's esp_codec_dev
// (es8311.c, Apache-2.0, SPDX-FileCopyrightText: 2023 Espressif Systems), as vendored in
// Waveshare's ESP32-S3-ePaper-1.54 audio example. Only what playback needs is kept.
#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace es8311 {

constexpr uint8_t kAddr = 0x18;  // 7-bit I2C address (CE pin low)

struct Coeff {
  uint32_t rate;
  uint8_t pre_div, pre_multi, adc_div, dac_div, fs_mode, lrck_h, lrck_l, bclk_div, adc_osr, dac_osr;
};
// MCLK = 256 x rate rows of the esp_codec_dev table.
static const Coeff kCoeffs[] = {
    {16000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {22050, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {32000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {44100, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {48000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
};

inline bool write(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(kAddr);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}
inline int read(uint8_t reg) {
  Wire.beginTransmission(kAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((int)kAddr, 1) != 1) return -1;
  return Wire.read();
}

inline bool present() { return read(0xFD) >= 0; }  // chip id registers 0xFD..0xFF (expect 0x83 0x11)

// Reset and configure for I2S slave mode with an external MCLK. The I2S master (and so MCLK)
// must already be running. Output stays muted until mute(false).
inline bool open() {
  bool ok = true;
  ok &= write(0x44, 0x08);
  ok &= write(0x44, 0x08);  // the first write after power-up sometimes fails
  ok &= write(0x01, 0x30);
  ok &= write(0x02, 0x00);
  ok &= write(0x03, 0x10);
  ok &= write(0x16, 0x24);
  ok &= write(0x04, 0x10);
  ok &= write(0x05, 0x00);
  ok &= write(0x0B, 0x00);
  ok &= write(0x0C, 0x00);
  ok &= write(0x10, 0x1F);
  ok &= write(0x11, 0x7F);
  ok &= write(0x00, 0x80);  // power up; bit 6 clear = slave mode
  ok &= write(0x01, 0x3F);  // clock source = MCLK pin, not inverted
  int r6 = read(0x06);
  if (r6 >= 0) ok &= write(0x06, r6 & ~0x20);  // SCLK not inverted
  ok &= write(0x13, 0x10);
  ok &= write(0x1B, 0x0A);
  ok &= write(0x1C, 0x6A);
  ok &= write(0x44, 0x58);  // internal reference (ADCL + DACR)
  return ok;
}

// 16-bit I2S (Philips) at `rate`, MCLK = 256 x rate.
inline bool setFormat(uint32_t rate) {
  const Coeff* c = nullptr;
  for (const Coeff& k : kCoeffs)
    if (k.rate == rate) c = &k;
  if (!c) return false;
  bool ok = true;
  int dac = read(0x09), adc = read(0x0A);
  if (dac < 0 || adc < 0) return false;
  dac |= 0x0C;  // 16 bit
  adc |= 0x0C;
  dac &= 0xFC;  // I2S format
  adc &= 0xFC;
  ok &= write(0x09, dac);
  ok &= write(0x0A, adc);
  int r2 = read(0x02) & 0x07;
  int multi = c->pre_multi == 1 ? 0 : c->pre_multi == 2 ? 1 : c->pre_multi == 4 ? 2 : 3;
  ok &= write(0x02, r2 | ((c->pre_div - 1) << 5) | (multi << 3));
  ok &= write(0x05, ((c->adc_div - 1) << 4) | (c->dac_div - 1));
  ok &= write(0x03, (read(0x03) & 0x80) | (c->fs_mode << 6) | c->adc_osr);
  ok &= write(0x04, (read(0x04) & 0x80) | c->dac_osr);
  ok &= write(0x07, (read(0x07) & 0xC0) | c->lrck_h);
  ok &= write(0x08, c->lrck_l);
  ok &= write(0x06, (read(0x06) & 0xE0) | (c->bclk_div < 19 ? c->bclk_div - 1 : c->bclk_div));
  return ok;
}

// Start the DAC path (playback only).
inline bool start() {
  bool ok = true;
  ok &= write(0x00, 0x80);
  ok &= write(0x01, 0x3F);
  int dac = read(0x09), adc = read(0x0A);
  if (dac < 0 || adc < 0) return false;
  dac &= 0xBF;
  adc &= 0xBF;
  adc |= 0x40;   // ADC path off
  dac &= ~0x40;  // DAC path on
  ok &= write(0x09, dac);
  ok &= write(0x0A, adc);
  ok &= write(0x17, 0xBF);
  ok &= write(0x0E, 0x02);
  ok &= write(0x12, 0x00);
  ok &= write(0x14, 0x1A);
  ok &= write(0x0D, 0x01);
  ok &= write(0x15, 0x40);
  ok &= write(0x37, 0x08);
  ok &= write(0x45, 0x00);
  return ok;
}

// Digital volume, -95.5 .. +32 dB in 0.5 dB steps.
inline void setVolumeDb(float db) {
  if (db < -95.5f) db = -95.5f;
  if (db > 32.0f) db = 32.0f;
  write(0x32, (uint8_t)((db + 95.5f) * 2.0f + 0.5f));
}
inline void mute(bool on) {
  int r = read(0x31);
  if (r < 0) return;
  r &= 0x9F;
  write(0x31, on ? (r | 0x60) : r);
}

// Power the codec down (the caller switches the supply off afterwards).
inline void stop() {
  write(0x32, 0x00);
  write(0x17, 0x00);
  write(0x0E, 0xFF);
  write(0x12, 0x02);
  write(0x14, 0x00);
  write(0x0D, 0xFA);
  write(0x15, 0x00);
  write(0x02, 0x10);
  write(0x00, 0x00);
  write(0x00, 0x1F);
  write(0x01, 0x30);
  write(0x01, 0x00);
  write(0x45, 0x00);
  write(0x0D, 0xFC);
  write(0x02, 0x00);
}

}  // namespace es8311

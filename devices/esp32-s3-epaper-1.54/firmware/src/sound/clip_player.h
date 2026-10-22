// Plays one bird clip: HTTPS download from the BirdNET Kids site, MP3 decode (Helix), speaker.
//
// Memory: the clip never sits in internal RAM. A ring buffer in PSRAM (allocated per call, freed
// afterwards) absorbs Wi-Fi jitter, playback starts once kPrefill bytes are in, and the decoder
// takes 1 KB at a time. Internal RAM use is about 110 KB at peak (TLS + decoder), whatever the
// clip size. Stage 1 of the sound POC (PLAN.md section 9) measured all of this.
//
// Wi-Fi must be connected by the caller. Header-only; include it from one file only.
#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "MP3DecoderHelix.h"
#include "audio_out.h"

#if defined(CLIP_LOG) || defined(DEBUG_LOG)
#define CLIP_PRINT(...) Serial.printf(__VA_ARGS__)
#else
#define CLIP_PRINT(...) ((void)0)
#endif

namespace clipplayer {

constexpr const char* kBaseUrl = "https://heladoo.github.io/Birdnet_kids/audio/";
constexpr size_t kRing = 160 * 1024;    // a whole clip is at most ~100 KB
constexpr size_t kPrefill = 32 * 1024;  // about 2 s at 128 kbit/s

using StopFn = bool (*)();  // return true to cut the clip short

// Played: sound came out. Cancelled: StopFn cut it short (do not try anything else).
// Unavailable: missing, empty or could not be started (the caller may try another recording).
enum class Result { Played, Cancelled, Unavailable };

namespace detail {
struct State {
  AudioOut audio;
  libhelix::MP3DecoderHelix mp3;
  float volumeDb = -15;
  uint32_t frames = 0, rate = 0, firstSoundMs = 0, t0 = 0;
  bool audioFailed = false;
};
inline State& S() {
  static State s;
  return s;
}

// Called by Helix for every decoded frame: `len` samples in total (all channels).
inline void onPcm(MP3FrameInfo& info, short* pcm, size_t len, void*) {
  State& s = S();
  if (!s.audio.isOpen() && !s.audioFailed) {
    s.rate = info.samprate;
    s.firstSoundMs = millis() - s.t0;
    s.audioFailed = !s.audio.begin(info.samprate, s.volumeDb);
    CLIP_PRINT("clip: first frame %d Hz, %d ch, %d kbit/s after %u ms; audio %s\n", info.samprate, info.nChans,
               info.bitrate / 1000, (unsigned)s.firstSoundMs, s.audioFailed ? "FAILED" : "on");
  }
  if (s.audioFailed) return;
  if (info.nChans == 2) {
    s.audio.write(pcm, len / 2);
    s.frames += len / 2;
  } else {  // mono: send each sample to both channels
    static int16_t stereo[1152 * 2];
    size_t n = len < 1152 ? len : 1152;
    for (size_t i = 0; i < n; i++) stereo[2 * i] = stereo[2 * i + 1] = pcm[i];
    s.audio.write(stereo, n);
    s.frames += n;
  }
}
}  // namespace detail

// Call once at boot (before any play): codec supply and amplifier off.
inline void idle() { detail::S().audio.idle(); }

// A short two-tone chirp (160 ms, 1000 then 1500 Hz): the "your hold was registered" cue, played
// the moment the button hold is recognised while the Wi-Fi is still connecting. The e-paper is not
// redrawn. It starts after 100 ms of silence: the first try (70 ms, no lead-in) was inaudible,
// because the codec ramps its output up after unmuting and swallowed it.
inline void chirp(float volumeDb) {
  detail::State& s = detail::S();
  constexpr uint32_t kRate = 16000;
  if (!s.audio.begin(kRate, volumeDb)) return;
  constexpr int kChunk = 256, kLead = 1600, kTone = 2560, kTail = 512;  // frames
  static int16_t buf[kChunk * 2];
  const float twoPi = 2.0f * (float)M_PI;
  float phase = 0;
  for (int pos = 0; pos < kLead + kTone + kTail; pos += kChunk) {
    for (int i = 0; i < kChunk; i++) {
      int t = pos + i - kLead;  // frames since the tone started
      int16_t v = 0;
      if (t >= 0 && t < kTone) {
        phase += twoPi * (t < kTone / 2 ? 1000.0f : 1500.0f) / kRate;
        if (phase > twoPi) phase -= twoPi;
        int edge = t < kTone - 1 - t ? t : kTone - 1 - t;  // 8 ms fades in and out
        float fade = edge < 128 ? edge / 128.0f : 1.0f;
        v = (int16_t)(sinf(phase) * 26000.0f * fade);
      }
      buf[2 * i] = buf[2 * i + 1] = v;
    }
    s.audio.write(buf, kChunk);
  }
  s.audio.end();
}

// Plays <kBaseUrl><name>.mp3. Returns true if sound was played, false if the clip is missing,
// empty or could not be started (the caller may then try another recording).
inline Result play(const char* name, float volumeDb, StopFn stop = nullptr) {
  detail::State& s = detail::S();
  s.volumeDb = volumeDb;
  s.frames = 0;
  s.rate = 0;
  s.firstSoundMs = 0;
  s.audioFailed = false;
  s.t0 = millis();

  uint8_t* ring = (uint8_t*)heap_caps_malloc(kRing, MALLOC_CAP_SPIRAM);
  if (!ring) {
    CLIP_PRINT("clip: no PSRAM for the ring buffer\n");
    return Result::Unavailable;
  }
  String url = String(kBaseUrl) + name + ".mp3";
  CLIP_PRINT("clip: %s\n", url.c_str());

  WiFiClientSecure client;
  client.setInsecure();  // public audio, only played; same choice as the data fetch
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    free(ring);
    return Result::Unavailable;
  }
  int code = http.GET();
  int size = http.getSize();
  CLIP_PRINT("clip: HTTP %d, %d bytes, headers after %u ms\n", code, size, (unsigned)(millis() - s.t0));
  if (code != 200 || size == 0) {  // some clips are empty files
    http.end();
    free(ring);
    return Result::Unavailable;
  }

  WiFi.setSleep(false);  // no modem power-save while streaming: lower jitter
  s.mp3.setDataCallback(detail::onPcm);
  s.mp3.begin();
  WiFiClient* net = http.getStreamPtr();

  size_t head = 0, tail = 0, fill = 0, minFill = kRing;
  uint32_t total = 0, underruns = 0, lastData = millis();
  bool eof = false, cancelled = false;
  auto pump = [&]() {  // non-blocking: move whatever the network has into the ring
    while (fill < kRing) {
      int avail = net->available();
      if (avail <= 0) break;
      size_t room = kRing - fill, contiguous = kRing - head, want = (size_t)avail;
      if (want > room) want = room;
      if (want > contiguous) want = contiguous;
      int n = net->readBytes(ring + head, want);
      if (n <= 0) break;
      head = (head + n) % kRing;
      fill += n;
      total += n;
      lastData = millis();
    }
    if (!net->connected() && net->available() == 0) eof = true;
  };

  while (fill < kPrefill && !eof && millis() - lastData < 15000) {
    pump();
    if (stop && stop()) {
      cancelled = true;
      break;
    }
    delay(2);
  }
  CLIP_PRINT("clip: prefill %u KB after %u ms\n", (unsigned)(fill / 1024), (unsigned)(millis() - s.t0));

  while (!cancelled && (fill > 0 || !eof)) {
    pump();
    if (stop && stop()) {
      cancelled = true;
      break;
    }
    if (fill == 0) {  // ran dry while the clip is not finished: an audible gap
      if (millis() - lastData > 15000) break;
      underruns++;
      delay(1);
      continue;
    }
    size_t n = fill < 1024 ? fill : 1024;
    if (n > kRing - tail) n = kRing - tail;
    s.mp3.write(ring + tail, n);
    tail = (tail + n) % kRing;
    fill -= n;
    if (!eof && fill < minFill) minFill = fill;
  }

  s.mp3.end();
  s.audio.end();
  http.end();
  free(ring);
  WiFi.setSleep(true);

  const float secs = s.rate ? (float)s.frames / s.rate : 0;
  CLIP_PRINT("clip: %u bytes, %.2f s of sound in %u ms, ring ran dry %u times, lowest fill %u KB%s; heap min %u KB\n",
             (unsigned)total, secs, (unsigned)(millis() - s.t0), (unsigned)underruns,
             (unsigned)(minFill == kRing ? 0 : minFill / 1024), cancelled ? ", CUT SHORT" : "",
             (unsigned)(ESP.getMinFreeHeap() / 1024));
  if (cancelled) return Result::Cancelled;
  return s.frames > 0 ? Result::Played : Result::Unavailable;
}

}  // namespace clipplayer

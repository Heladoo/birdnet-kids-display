// Sound POC, stage 1: stream one MP3 clip from GitHub Pages, decode it on the fly (Helix) and
// play it through the speaker. Memory: a 160 KB ring buffer in PSRAM absorbs Wi-Fi jitter; the
// decoder takes 1 KB at a time from it and the PCM goes straight to the I2S DMA. Internal RAM
// use stays at about 110 KB peak (TLS + decoder), whatever the clip size.
//
// Build: `pio run -e sound_stream -t upload`, then watch the serial monitor. Needs the Wi-Fi
// details in include/secrets.h. Plays one clip at boot; serial 'n' / BOOT / PWR plays the next.
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "MP3DecoderHelix.h"
#include "audio_out.h"
#include "secrets.h"

constexpr float kVolumeDb = -15;
constexpr size_t kRing = 160 * 1024;    // PSRAM jitter buffer (a whole clip is at most ~100 KB)
constexpr size_t kPrefill = 32 * 1024;  // bytes buffered before playback starts (~2 s at 128 kbit/s)
constexpr int kBtnBoot = 0, kBtnPwr = 18;
static const char* kBase = "https://heladoo.github.io/Birdnet_kids/audio/";
static const char* kClips[] = {"upupa_epops", "turdus_merula", "motacilla_alba", "upupa_epops_2"};
constexpr int kNumClips = sizeof kClips / sizeof kClips[0];

static AudioOut audio;
static uint8_t* ring = nullptr;
static libhelix::MP3DecoderHelix mp3;

// Per-clip statistics, filled by the decoder callback.
static struct {
  uint32_t t0 = 0, firstSoundMs = 0, frames = 0, rate = 0, chans = 0, bitrate = 0;
  bool audioFailed = false;
} clip;

// Called by Helix for every decoded frame: `len` samples in total (all channels).
static void onPcm(MP3FrameInfo& info, short* pcm, size_t len, void*) {
  if (!audio.isOpen() && !clip.audioFailed) {
    clip.rate = info.samprate;
    clip.chans = info.nChans;
    clip.bitrate = info.bitrate;
    clip.firstSoundMs = millis() - clip.t0;
    clip.audioFailed = !audio.begin(info.samprate, kVolumeDb);
    Serial.printf("first frame: %d Hz, %d ch, %d kbit/s after %u ms; audio %s\n", info.samprate, info.nChans,
                  info.bitrate / 1000, (unsigned)clip.firstSoundMs, clip.audioFailed ? "FAILED" : "on");
  }
  if (clip.audioFailed) return;
  if (info.nChans == 2) {
    audio.write(pcm, len / 2);
    clip.frames += len / 2;
  } else {  // mono: send each sample to both channels
    static int16_t stereo[1152 * 2];
    size_t n = len < 1152 ? len : 1152;
    for (size_t i = 0; i < n; i++) stereo[2 * i] = stereo[2 * i + 1] = pcm[i];
    audio.write(stereo, n);
    clip.frames += n;
  }
}

static bool wifiConnect() {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi: FAILED");
    return false;
  }
  Serial.printf("Wi-Fi: connected in %u ms, RSSI %d dBm\n", (unsigned)(millis() - t0), WiFi.RSSI());
  return true;
}

static void playClip(const char* name) {
  if (!wifiConnect()) return;
  String url = String(kBase) + name + ".mp3";
  Serial.printf("--- %s\n", url.c_str());
  clip = {};
  clip.t0 = millis();
  const uint32_t heapBefore = ESP.getFreeHeap();

  WiFiClientSecure client;
  client.setInsecure();  // public audio, only played; same choice as the display firmware
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    Serial.println("http.begin failed");
    return;
  }
  int code = http.GET();
  Serial.printf("HTTP %d, %d bytes, headers after %u ms\n", code, http.getSize(), (unsigned)(millis() - clip.t0));
  if (code != 200) {
    http.end();
    return;
  }

  WiFi.setSleep(false);  // no modem power-save while streaming: lower jitter
  mp3.begin();
  WiFiClient* net = http.getStreamPtr();

  // Jitter buffer in PSRAM: Wi-Fi delivers in bursts (gaps of 200 ms were measured) while
  // playback needs a steady 16 KB/s. Read ahead into a ring, start decoding once kPrefill bytes
  // (about 2 s of audio) are in, and keep topping the ring up between decode steps.
  size_t head = 0, tail = 0, fill = 0, minFill = kRing;
  uint32_t total = 0, underruns = 0, lastData = millis();
  bool eof = false;
  auto pump = [&]() {  // non-blocking: move whatever the network has into the ring
    while (fill < kRing) {
      int avail = net->available();
      if (avail <= 0) break;
      size_t room = kRing - fill, contiguous = kRing - head;
      size_t want = (size_t)avail;
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
    delay(2);
  }
  const uint32_t prefillMs = millis() - clip.t0;
  Serial.printf("prefill: %u KB in ring after %u ms\n", (unsigned)(fill / 1024), (unsigned)prefillMs);

  while (fill > 0 || !eof) {
    pump();
    if (fill == 0) {  // ran dry while the clip is not finished: an audible gap
      if (millis() - lastData > 15000) break;
      underruns++;
      delay(1);
      continue;
    }
    size_t n = fill < 1024 ? fill : 1024;
    if (n > kRing - tail) n = kRing - tail;
    mp3.write(ring + tail, n);
    tail = (tail + n) % kRing;
    fill -= n;
    if (!eof && fill < minFill) minFill = fill;
  }
  mp3.end();
  audio.end();
  http.end();
  WiFi.setSleep(true);

  const float secs = clip.rate ? (float)clip.frames / clip.rate : 0;
  Serial.printf("done: %u bytes read, %u frames = %.2f s of sound, total %u ms\n", (unsigned)total,
                (unsigned)clip.frames, secs, (unsigned)(millis() - clip.t0));
  Serial.printf("ring ran dry %u times; lowest fill while playing %u KB of %u KB\n", (unsigned)underruns,
                (unsigned)(minFill == kRing ? 0 : minFill / 1024), (unsigned)(kRing / 1024));
  Serial.printf("heap: %u KB free before, minimum since boot %u KB, largest block now %u KB\n",
                (unsigned)(heapBefore / 1024), (unsigned)(ESP.getMinFreeHeap() / 1024),
                (unsigned)(ESP.getMaxAllocHeap() / 1024));
}

static int next = 0;
static void playNext() { playClip(kClips[next++ % kNumClips]); }

void setup() {
  audio.idle();
  pinMode(kBtnBoot, INPUT_PULLUP);
  pinMode(kBtnPwr, INPUT_PULLUP);
  Serial.begin(115200);
  delay(3000);
  Serial.println("sound_stream: stage 1 of the sound POC");
  Serial.printf("heap at start: %u KB free\n", (unsigned)(ESP.getFreeHeap() / 1024));
  ring = (uint8_t*)heap_caps_malloc(kRing, MALLOC_CAP_SPIRAM);
  Serial.printf("PSRAM ring buffer %u KB: %s (PSRAM free %u KB)\n", (unsigned)(kRing / 1024), ring ? "ok" : "FAILED",
                (unsigned)(ESP.getFreePsram() / 1024));
  if (!ring) return;
  mp3.setDataCallback(onPcm);
  playNext();
}

void loop() {
  bool go = false;
  while (Serial.available()) go |= Serial.read() == 'n';
  if (digitalRead(kBtnBoot) == LOW || digitalRead(kBtnPwr) == LOW) {
    go = true;
    while (digitalRead(kBtnBoot) == LOW || digitalRead(kBtnPwr) == LOW) delay(10);
  }
  if (go) playNext();
  delay(20);
}

// Sound POC, stage 1 test program: play bird clips from GitHub Pages, using the same player
// (clip_player.h) as the real firmware.
//
// Build: `pio run -e sound_stream -t upload`, then watch the serial monitor. Needs the Wi-Fi
// details in include/secrets.h. Plays one clip at boot; serial 'n' / BOOT / PWR plays the next.
#include <Arduino.h>
#include <WiFi.h>
#include "clip_player.h"
#include "secrets.h"

constexpr float kVolumeDb = -15;
constexpr int kBtnBoot = 0, kBtnPwr = 18;
static const char* kClips[] = {"upupa_epops", "turdus_merula", "motacilla_alba", "upupa_epops_2"};
constexpr int kNumClips = sizeof kClips / sizeof kClips[0];

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

static int next = 0;
static void playNext() {
  if (wifiConnect()) clipplayer::play(kClips[next++ % kNumClips], kVolumeDb);
}

void setup() {
  clipplayer::idle();
  pinMode(kBtnBoot, INPUT_PULLUP);
  pinMode(kBtnPwr, INPUT_PULLUP);
  Serial.begin(115200);
  delay(3000);
  Serial.println("sound_stream: stage 1 of the sound POC");
  Serial.printf("heap at start: %u KB free, PSRAM %u KB free\n", (unsigned)(ESP.getFreeHeap() / 1024),
                (unsigned)(ESP.getFreePsram() / 1024));
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

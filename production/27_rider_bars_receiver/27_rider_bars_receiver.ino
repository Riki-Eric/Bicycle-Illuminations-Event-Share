/*
 * 自転車2台の表示用ESP32に同じプログラムを書き込み、43番のA/B RPMを無線で受信する。
 * LED直付け版はGPIO 18,19,21,22,23,25,26,27 → 直列抵抗R1〜R8 → 基板上のLED D1〜D8の＋。
 * LEDの−は四角いGNDパッドへ。抵抗は各LEDに合わせる。DevKitC V4はUSBで給電する。
 * 115200 bps・改行あり。「role」で確認。「role a」は自転車A（初期値）、
 * 「role b」は自転車B。役割は電源を切っても残る。
 * 旧74HC595基板のピン設定と「role dual」は RIDER_BAR_USE_74HC595=1 でビルドして使う。
 * シリアルモニタの help で一覧。status は受信状態・最終受信からの時間・A/B RPM・出力マスク。
 * watch 1000 で1秒ごとの表示、watch off で停止。config は現在値、config export は復元用set行。
 * test はD1〜D8を順番に、test 3 はD3だけ点灯。stop で消灯、p で回転表示に復帰。
 * 左から緑緑黄黄黄赤赤赤。test 1 と test 8で低速側の左端と高速側の右端を確認する。
 * config full_a / set full_a 90 等で8灯になるRPMを確認・調整する。
 * 最後に受信してから750 msで消灯。データが戻れば表示を再開する。
 */
#include "src/RiderBarDisplay.h"
#include "src/RiderBarRole.h"
#include "src/RiderSpeedRadio.h"
#include "src/OpeningShow/LocalControls.h"
#include "src/OpeningShow/Settings.h"
#include <freertos/queue.h>

constexpr uint32_t LINK_TIMEOUT_MS = 750;
enum DisplaySetting { FullA, FullB, Reverse };
OpeningShow::Setting options[] = {
    {"full_a", 90, 1, 1000}, {"full_b", 90, 1, 1000}, {"reverse", 0, 0, 1},
};
OpeningShow::Settings config("tbt-bar-fx", options);
OpeningShow::Console console;
OpeningShow::ConsoleWatch watch(1000);

struct SpeedSample {
  RiderSpeedRadio::Packet packet;
  uint32_t receivedAt;
};

static RiderBarDisplay riderBars;
static RiderBarRole role;
static QueueHandle_t samples;
static SpeedSample latest{};
static bool linkActive = false, sampled = false, radioReady = false;
static bool enabled = true, testing = false;
static uint8_t testLed = 0;
static uint32_t testAt = 0;

void receiveSpeed(const esp_now_recv_info_t *, const uint8_t *data, int length) {
  SpeedSample sample{};
  if (length != static_cast<int>(sizeof(sample.packet))) return;
  memcpy(&sample.packet, data, sizeof(sample.packet));
  if (sample.packet.tag != RiderSpeedRadio::PACKET_TAG) return;
  sample.receivedAt = millis();
  xQueueOverwrite(samples, &sample);
}

uint8_t barLevel(uint16_t rpmTenths, uint16_t fullRpm) {
  if (!rpmTenths) return 0;
  if (rpmTenths >= 10UL * fullRpm) return 8;
  return 1 + (7UL * rpmTenths) / (10UL * fullRpm);
}

void help() {
  Serial.println("27 RPM display | 115200 bps, newline | status: RX age, A/B RPM, display state and LED bits");
  Serial.println("role [a|b]: read/set bike | test: scan D1..D8 | test 1: low end | test 8: high end | test N: one LED");
  Serial.println("D1..D8: G G Y Y Y R R R | stop: blackout | p: resume RPM display");
  OpeningShow::ConsoleWatch::help();
  OpeningShow::Settings::help();
}

uint8_t pattern(uint8_t level) {
  uint8_t mask = (1U << level) - 1;
  if (config[Reverse] && level) mask <<= 8 - level;
  return mask;
}

void setup() {
  riderBars.begin();
  Serial.begin(115200);
  config.begin();
  role.begin(RiderBarDisplay::SUPPORTS_DUAL);
  samples = xQueueCreate(1, sizeof(SpeedSample));
  radioReady = samples && RiderSpeedRadio::begin() && esp_now_register_recv_cb(receiveSpeed) == ESP_OK;
  Serial.printf("RPM receiver (channel %u) %s\n", RiderSpeedRadio::CHANNEL,
                radioReady ? "waiting for sender" : "radio init failed");
  help();
  Serial.printf("27 assigned bike: %s\n", role.name());
}

void loop() {
  if (samples && xQueueReceive(samples, &latest, 0) == pdTRUE) linkActive = sampled = true;
  const uint32_t now = millis();
  bool status = false;
  if (const char *cmd = console.read()) {
    if (!watch.command(cmd, now) && !config.command(cmd, !testing) && !role.command(cmd)) {
      if (!strcmp(cmd, "status")) status = true;
      else if (!strcmp(cmd, "test") || (!strncmp(cmd, "test ", 5) && strlen(cmd) == 6 &&
          cmd[5] >= '1' && cmd[5] <= '8')) {
        testLed = cmd[4] ? cmd[5] - '0' : 0;
        testing = true;
        testAt = now;
        if (testLed) Serial.printf("27 role=%s D%u only, 1.6s\n", role.name(), testLed);
        else Serial.printf("27 role=%s D1..D8 in order, 3.2s\n", role.name());
      }
      else if (!strcmp(cmd, "stop")) enabled = testing = false;
      else if (!strcmp(cmd, "p")) { enabled = true; testing = false; }
      else help();
    }
  }
  if (linkActive && now - latest.receivedAt >= LINK_TIMEOUT_MS) linkActive = false;
  const uint16_t rpmA = linkActive ? latest.packet.rpmTenthsA : 0;
  const uint16_t rpmB = linkActive ? latest.packet.rpmTenthsB : 0;
  const uint8_t levelA = barLevel(rpmA, config[FullA]);
  const uint8_t levelB = barLevel(rpmB, config[FullB]);
  const uint32_t testAge = now - testAt;
  if (testing && testAge >= (testLed ? 1600U : 3200U)) testing = false;
  uint8_t a = enabled ? pattern(role.mode() == RiderBarRole::B ? levelB : levelA) : 0;
  uint8_t b = enabled && role.mode() == RiderBarRole::Dual ? pattern(levelB) : 0;
  if (testing) {
    a = 1U << (testLed ? testLed - 1 : testAge / 400);
    b = role.mode() == RiderBarRole::Dual ? a : 0;
  }
  riderBars.write(a, b);

  if (status || watch.due(now)) {
    watch.printed(now);
    char age[11] = "-";
    if (sampled) snprintf(age, sizeof(age), "%lu", (unsigned long)(now - latest.receivedAt));
    Serial.printf("role=%s rx=%s age_ms=%s A=%u.%u rpm (%u/8) B=%u.%u rpm (%u/8) display=%s LED=%02X/%02X\n",
        role.name(), !radioReady ? "error" : linkActive ? "ok" : sampled ? "stale" : "waiting",
        age, rpmA / 10U, rpmA % 10U, unsigned(levelA), rpmB / 10U, rpmB % 10U, unsigned(levelB),
        testing ? "test" : enabled ? "live" : "off", unsigned(a), unsigned(b));
  }
  delay(5);
}

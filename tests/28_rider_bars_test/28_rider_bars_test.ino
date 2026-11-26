/*
 * 表示用ESP32に書き込み、センサーや送信機なしで実際のLEDを確認する。
 * LED直付け版はGPIO 18,19,21,22,23,25,26,27 → 直列抵抗R1〜R8 → 基板上のLED D1〜D8の＋。
 * LEDの−は四角いGNDパッドへ。抵抗は各LEDに合わせる。DevKitC V4はUSBで給電する。
 * D1からD8の順に点灯した後、点灯範囲の伸縮と全点灯・消灯を繰り返す。
 * 115200 bps・改行あり。「role」で確認、「role a」／「role b」で役割を保存（27番とも共用）。
 * 旧74HC595基板を試す場合は RIDER_BAR_USE_74HC595=1 でビルドする。
 */
#include "src/RiderBarDisplay.h"
#include "src/RiderBarRole.h"

static RiderBarDisplay riderBars;
static RiderBarRole role;

void setup() {
  riderBars.begin();
  Serial.begin(115200);
  role.begin(RiderBarDisplay::SUPPORTS_DUAL);
}

// Return early on a role change so the new bar sequence starts at LED 1.
bool waitForStep(uint32_t duration) {
  const uint32_t start = millis();
  while (millis() - start < duration) {
    if (role.poll()) {
      riderBars.write(0, 0);
      return false;
    }
    delay(5);
  }
  return true;
}

void loop() {
  const bool dual = role.mode() == RiderBarRole::Dual;
  for (uint8_t led = 0; led < (dual ? 16 : 8); ++led) {
    const uint8_t index = led % 8;
    const uint8_t bit = 1U << index;
    const bool barA = led < 8;
    riderBars.write(barA ? bit : 0, barA ? 0 : bit);
    Serial.printf("role=%s %s LED %u\n", role.name(), barA ? "JA" : "JB", index + 1U);
    if (!waitForStep(250)) return;
  }

  Serial.println(dual ? "JA grows, JB shrinks" : "JA grows");
  for (uint8_t level = 0; level <= 8; ++level) {
    riderBars.showLevels(level, dual ? 8 - level : 0);
    if (!waitForStep(250)) return;
  }
  if (!dual) {
    Serial.println("JA shrinks");
    for (uint8_t level = 8; level > 0; --level) {
      riderBars.showLevels(level - 1, 0);
      if (!waitForStep(250)) return;
    }
  }
  Serial.println(dual ? "All 16 LEDs on" : "All 8 LEDs on");
  riderBars.showLevels(8, dual ? 8 : 0);
  if (!waitForStep(1500)) return;
  Serial.println("All off");
  riderBars.showLevels(0, 0);
  waitForStep(1000);
}

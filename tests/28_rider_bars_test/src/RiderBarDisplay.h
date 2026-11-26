#pragma once

#include <Arduino.h>

#ifndef RIDER_BAR_USE_74HC595
#define RIDER_BAR_USE_74HC595 0
#endif

class RiderBarDisplay {
 public:
  static constexpr bool SUPPORTS_DUAL = RIDER_BAR_USE_74HC595 != 0;

  void begin() {
#if RIDER_BAR_USE_74HC595
    pinMode(ENABLE_PIN, INPUT);
    // The external 3V3 pull-up keeps /OE high until the blank frame is latched.
    pinMode(DATA_PIN, OUTPUT);
    pinMode(CLOCK_PIN, OUTPUT);
    pinMode(LATCH_PIN, OUTPUT);
    digitalWrite(CLOCK_PIN, LOW);
    digitalWrite(LATCH_PIN, LOW);
    write(0, 0);
    pinMode(ENABLE_PIN, OUTPUT_OPEN_DRAIN);
    digitalWrite(ENABLE_PIN, LOW);
#else
    for (uint8_t pin : LED_PINS) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
    }
#endif
    lastA_ = lastB_ = 0;
  }

  void showLevels(uint8_t a, uint8_t b) {
    write(mask(a), mask(b));
  }

  void write(uint8_t a, uint8_t b) {
#if RIDER_BAR_USE_74HC595
    if (a == lastA_ && b == lastB_) return;
    lastA_ = a;
    lastB_ = b;
    digitalWrite(LATCH_PIN, LOW);
    shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, b);  // Far register (B) first.
    shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, a);
    digitalWrite(LATCH_PIN, HIGH);
#else
    (void)b;
    const uint8_t changed = a ^ lastA_;
    for (uint8_t i = 0; i < sizeof(LED_PINS); ++i) {
      const uint8_t bit = 1U << i;
      if (changed & bit) digitalWrite(LED_PINS[i], (a & bit) ? HIGH : LOW);
    }
    lastA_ = a;
#endif
  }

 private:
#if RIDER_BAR_USE_74HC595
  static constexpr uint8_t DATA_PIN = 27, CLOCK_PIN = 13, LATCH_PIN = 14, ENABLE_PIN = 17;
#else
  static constexpr uint8_t LED_PINS[] = {18, 19, 21, 22, 23, 25, 26, 27};
#endif

  static uint8_t mask(uint8_t level) {
    return level >= 8 ? 0xff : uint8_t((1U << level) - 1);
  }

  uint8_t lastA_ = 255, lastB_ = 255;
};

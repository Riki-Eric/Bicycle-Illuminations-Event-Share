#pragma once
#include <Arduino.h>

class DrapeSerial {
 public:
  enum Command : uint8_t { None, Play, Stop };

  Command poll(uint32_t now) {
    if (Serial.available() <= 0) return None;

    // An AVR NeoPixel write can overrun serial RX. Handle the first byte of
    // "stop" and ignore a trailing 'p' that belongs to the same send.
    const int command = Serial.read();
    if (command == 's') {
      textStopAt_ = now;
      return Stop;
    }
    if (command == '0') return Stop;
    if (command == 'p' && now - textStopAt_ >= StopTailMs) return Play;
    return None;
  }

 private:
  static constexpr uint16_t StopTailMs = 30;
  uint32_t textStopAt_ = 0UL - StopTailMs;
};

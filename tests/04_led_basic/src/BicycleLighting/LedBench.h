#pragma once
#include "StripOutputs.h"
class LedBench {
 public:
  void begin() {
    Serial.begin(BikeConfig::SERIAL_BAUD);
    ready_ = strip_.begin();
    Serial.println(ready_ ? F("LED OFF. 1=R 2=G 3=B 4=white c=center-out i=split R/B 0=OFF")
                         : F("ERROR: LED init"));
  }
  void tick() {
    if (!ready_) return;
    if (Serial.available()) {
      const char ch = char(Serial.read());
      if (ch >= '0' && ch <= '4') { test_ = ch - '0'; dirty_ = true; }
      else if (ch == 'c') { test_ = 5; step_ = 0; dirty_ = true; }
      else if (ch == 'i') { test_ = 6; dirty_ = true; }
    }
    const uint32_t now = millis();
    if (test_ == 5 && uint32_t(now - lastStep_) >= 150) { lastStep_ = now; ++step_; dirty_ = true; }
    if (dirty_ && uint32_t(now - lastFrame_) >= BikeConfig::LED_MIN_INTERVAL_MS) {
      lastFrame_ = now; strip_.basic(test_, step_); dirty_ = false;
    }
  }
 private:
  StripOutputs strip_;
  bool ready_ = false, dirty_ = false;
  uint8_t test_ = 0;
  uint16_t step_ = 0;
  uint32_t lastStep_ = 0, lastFrame_ = 0;
};

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <cstring>

class RiderBarRole {
 public:
  enum Mode : uint8_t { Dual, A, B };

  void begin(bool supportsDual) {
    supportsDual_ = supportsDual;
    canSave_ = prefs_.begin("rider-bars", false);
    const Mode defaultMode = supportsDual_ ? Dual : A;
    const uint8_t saved = canSave_ ? prefs_.getUChar("mode", defaultMode) : defaultMode;
    mode_ = saved == A || saved == B ? static_cast<Mode>(saved) : defaultMode;
    Serial.println("role a = bicycle A on JA; role b = bicycle B on JA");
    if (supportsDual_) Serial.println("role dual = A on JA / B on JB");
    if (!canSave_) Serial.println("Role storage unavailable");
    report();
  }

  Mode mode() const { return mode_; }
  const char *name() const { return mode_ == A ? "a" : mode_ == B ? "b" : "dual"; }

  bool poll() {
    bool changed = false;
    while (Serial.available()) {
      const char c = static_cast<char>(Serial.read());
      if (c == '\r' || c == '\n') {
        if (length_ && !overflow_) {
          command_[length_] = '\0';
          if (execute()) changed = true;
        }
        length_ = 0;
        overflow_ = false;
      } else if (length_ + 1 < sizeof(command_)) {
        command_[length_++] = c;
      } else {
        overflow_ = true;
      }
    }
    return changed;
  }

 private:
  Preferences prefs_;
  Mode mode_ = Dual;
  bool supportsDual_ = false, canSave_ = false, overflow_ = false;
  char command_[16]{};
  size_t length_ = 0;

  void report() const { Serial.printf("role=%s\n", name()); }

  bool execute() {
    if (strcmp(command_, "role") == 0) {
      report();
      return false;
    }
    Mode selected;
    if (strcmp(command_, "role a") == 0) selected = A;
    else if (strcmp(command_, "role b") == 0) selected = B;
    else if (supportsDual_ && strcmp(command_, "role dual") == 0) selected = Dual;
    else {
      Serial.println(supportsDual_ ? "Expected: role | role a | role b | role dual"
                                    : "Expected: role | role a | role b");
      return false;
    }
    if (selected == mode_) {
      report();
      return false;
    }
    if (!canSave_ || prefs_.putUChar("mode", selected) != sizeof(uint8_t)) {
      Serial.println("Could not save requested role");
      report();
      return false;
    }
    mode_ = selected;
    report();
    return true;
  }
};

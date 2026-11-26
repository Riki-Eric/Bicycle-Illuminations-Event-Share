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

  bool command(const char *cmd) {
    if (strcmp(cmd, "role") == 0) {
      report();
      return true;
    }
    if (strncmp(cmd, "role ", 5)) return false;
    Mode selected;
    if (strcmp(cmd, "role a") == 0) selected = A;
    else if (strcmp(cmd, "role b") == 0) selected = B;
    else if (supportsDual_ && strcmp(cmd, "role dual") == 0) selected = Dual;
    else {
      Serial.println(supportsDual_ ? "Expected: role | role a | role b | role dual"
                                    : "Expected: role | role a | role b");
      return true;
    }
    if (selected == mode_) {
      report();
      return true;
    }
    if (!canSave_ || prefs_.putUChar("mode", selected) != sizeof(uint8_t)) {
      Serial.println("Could not save requested role");
      report();
      return true;
    }
    mode_ = selected;
    report();
    return true;
  }

 private:
  Preferences prefs_;
  Mode mode_ = Dual;
  bool supportsDual_ = false, canSave_ = false;

  void report() const { Serial.printf("role=%s\n", name()); }
};

#pragma once

#include <Arduino.h>
#include <Preferences.h>

namespace OpeningShow {
struct Setting {
  const char *name;
  uint32_t initial, minimum, maximum;
  uint32_t value = initial;
};

class Settings {
 public:
  template <size_t N>
  Settings(const char *area, Setting (&entries)[N]) : Settings(area, entries, N) {}
  Settings(const char *area, Setting *entries, size_t count)
      : area_(area), entries_(entries), count_(count) {}

  void begin() {
    Preferences prefs;
    if (!prefs.begin(area_, true)) return;
    for (size_t i = 0; i < count_; ++i) {
      Setting &entry = entries_[i];
      const uint32_t saved = prefs.getUInt(entry.name, entry.initial);
      if (saved >= entry.minimum && saved <= entry.maximum) entry.value = saved;
    }
    prefs.end();
  }

  uint32_t operator[](size_t index) const { return entries_[index].value; }

  void print() const {
    for (size_t i = 0; i < count_; ++i) {
      print(entries_[i]);
    }
  }

  static void help() {
    Serial.println("config [name|export] | set <name> <number> | default <name>");
  }

  bool command(const char *cmd, bool idle) {
    if (!strcmp(cmd, "config")) { print(); return true; }
    if (!strcmp(cmd, "config export")) {
      for (size_t i = 0; i < count_; ++i) {
        const Setting &e = entries_[i];
        Serial.printf("set %s %lu\n", e.name, (unsigned long)e.value);
      }
      return true;
    }
    if (!strncmp(cmd, "config ", 7)) {
      for (size_t i = 0; i < count_; ++i) {
        if (!strcmp(cmd + 7, entries_[i].name)) { print(entries_[i]); return true; }
      }
      Serial.println("ERR unknown setting; use config");
      return true;
    }
    const bool reset = !strncmp(cmd, "default ", 8);
    if (!reset && strncmp(cmd, "set ", 4)) return false;
    if (!idle) {
      Serial.println("ERR stop playback before changing settings");
      return true;
    }
    char key[16], input[16], extra;
    const int fields = sscanf(cmd + (reset ? 8 : 4), "%15s %15s %c", key, input, &extra);
    if (fields != (reset ? 1 : 2)) {
      Serial.println("Use config | set <name> <number> | default <name>");
      return true;
    }
    for (size_t i = 0; i < count_; ++i) {
      Setting &e = entries_[i];
      if (strcmp(key, e.name)) continue;
      uint32_t value = e.initial;
      if (!reset && !number(input, e.minimum, e.maximum, value)) {
        Serial.printf("ERR %s range %lu..%lu\n", e.name, (unsigned long)e.minimum,
                      (unsigned long)e.maximum);
        return true;
      }
      Preferences prefs;
      if (value != e.value) {
        if (!prefs.begin(area_, false) || prefs.putUInt(e.name, value) != sizeof(value)) {
          Serial.println("ERR save failed");
          return true;
        }
        e.value = value;
      }
      Serial.printf("saved %s=%lu\n", e.name, (unsigned long)e.value);
      return true;
    }
    Serial.println("ERR unknown setting; use config");
    return true;
  }

  static bool number(const char *text, uint32_t minimum, uint32_t maximum, uint32_t &value) {
    const bool hex = text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    const char *digits = text + (hex ? 2 : 0);
    if (!digits[0] || digits[strspn(digits, hex ? "0123456789abcdefABCDEF" : "0123456789")])
      return false;
    uint64_t parsed = 0;
    const uint8_t radix = hex ? 16 : 10;
    for (const char *p = digits; *p; ++p) {
      const uint8_t digit = *p <= '9' ? *p - '0' : (*p | 0x20) - 'a' + 10;
      parsed = parsed * radix + digit;
      if (parsed > maximum) return false;
    }
    if (parsed < minimum) return false;
    value = parsed;
    return true;
  }

 private:
  static void print(const Setting &e) {
    Serial.printf("%s=%lu [%lu..%lu] default=%lu\n", e.name, (unsigned long)e.value,
        (unsigned long)e.minimum, (unsigned long)e.maximum, (unsigned long)e.initial);
  }
  const char *area_;
  Setting *entries_;
  size_t count_;
};
}

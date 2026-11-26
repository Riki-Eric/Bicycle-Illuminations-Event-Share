#pragma once
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

namespace OpeningShow {
class Contact {
 public:
  void begin(uint8_t pin, uint8_t mode = INPUT) {
    pin_ = pin;
    pinMode(pin_, mode);
    raw_ = stable_ = down();
    changed_ = millis();
  }
  bool down() const { return digitalRead(pin_) == LOW; }
  bool pressed(uint32_t now) {
    const bool current = down();
    if (current != raw_) { raw_ = current; changed_ = now; }
    if (stable_ == raw_ || now - changed_ < 35) return false;
    stable_ = raw_;
    return stable_;
  }

 private:
  uint8_t pin_ = 0;
  bool raw_ = false, stable_ = false;
  uint32_t changed_ = 0;
};

template <size_t Capacity>
class LineConsole {
 public:
  const char *read() {
    for (size_t budget = 64; budget && Serial.available(); --budget) {
      const char ch = Serial.read();
      if (ch == '\r' || ch == '\n') {
        while (used_ && space(buffer_[used_ - 1])) --used_;
        const bool complete = used_ && !invalid_;
        const bool rejected = invalid_;
        buffer_[used_] = '\0';
        used_ = 0;
        invalid_ = false;
        if (rejected) Serial.println("ERR serial line too long or contains a control character");
        if (complete) return buffer_;
      } else if (!invalid_ && (used_ || !space(ch))) {
        if (used_ == sizeof(buffer_) - 1 || (uint8_t(ch) < 32 && ch != '\t') || ch == 127)
          invalid_ = true;
        else buffer_[used_++] = ch;
      }
    }
    return nullptr;
  }

 private:
  static bool space(char ch) { return ch == ' ' || ch == '\t'; }
  char buffer_[Capacity]{};
  size_t used_ = 0;
  bool invalid_ = false;
};

using Console = LineConsole<48>;

class ConsoleWatch {
 public:
  explicit ConsoleWatch(uint16_t interval = 0) : interval_(interval) {}

  bool command(const char *cmd, uint32_t now) {
    if (!strcmp(cmd, "watch off")) interval_ = 0;
    else if (!strncmp(cmd, "watch ", 6)) {
      const char *value = cmd + 6;
      const size_t length = strlen(value);
      const unsigned long parsed = length && length <= 4 && strspn(value, "0123456789") == length
          ? strtoul(value, nullptr, 10) : 0;
      if (parsed < 500 || parsed > 5000) {
        Serial.println("Use watch | watch <500..5000 ms> | watch off");
        return true;
      }
      interval_ = parsed;
      last_ = now;
    } else if (strcmp(cmd, "watch")) return false;
    if (interval_) Serial.printf("watch=%u ms\n", interval_);
    else Serial.println("watch=off");
    return true;
  }

  bool due(uint32_t now) const { return interval_ && now - last_ >= interval_; }
  void printed(uint32_t now) { last_ = now; }

  static void help() { Serial.println("watch: watch 1000 (500..5000 ms) | watch off"); }

 private:
  uint32_t last_ = 0;
  uint16_t interval_;
};

class BoardLights {
 public:
  BoardLights(uint8_t run, uint8_t link, uint8_t show, uint8_t fault)
      : run_(run), link_(link), show_(show), fault_(fault) {}
  void begin() const {
    for (uint8_t pin : {run_, link_, show_, fault_}) {
      digitalWrite(pin, LOW);
      pinMode(pin, OUTPUT);
    }
  }
  void update(bool ready, bool link, bool show, bool fault) const {
    digitalWrite(run_, ready);
    digitalWrite(link_, link);
    digitalWrite(show_, show);
    digitalWrite(fault_, fault);
  }
 private:
  uint8_t run_, link_, show_, fault_;
};
}

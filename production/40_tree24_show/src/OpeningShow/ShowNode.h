#pragma once
#include "ShowRadio.h"
#include <Preferences.h>

namespace OpeningShow {
inline const char *phaseLabel(Phase phase) {
  static constexpr const char *labels[] = {"OFF", "ARMED", "TRAIL", "RIDE", "WHITE", "SPARKLE",
      "DRAPE", "HOLD", "SHORT HOLD", "FADE", "TREE CHARGE", "STEADY"};
  return labels[uint8_t(phase)];
}

class ShowNode {
 public:
  void begin(Unit first, Unit last) {
    first_ = first;
    last_ = last;
    if (first == last) unit_ = first;
    else {
      Preferences settings;
      if (settings.begin("tbt-node", true)) {
        const auto saved = static_cast<Unit>(settings.getUChar("unit", UNIT_COUNT));
        settings.end();
        if (saved >= first && saved <= last) unit_ = saved;
      }
    }
    radio.begin();
  }

  bool select(uint8_t oneBased) {
    if (first_ == last_ || !oneBased || oneBased > uint8_t(last_) - uint8_t(first_) + 1 ||
        (follow.packet().phase != Phase::Off && follow.linked(millis()))) return false;
    Preferences settings;
    if (!settings.begin("tbt-node", false)) return false;
    const auto unit = uint8_t(first_) + oneBased - 1;
    const bool stored = settings.putUChar("unit", unit) == 1;
    settings.end();
    if (stored) unit_ = static_cast<Unit>(unit);
    return stored;
  }

  bool receive(uint32_t now, RideRate *receivedRate = nullptr) {
    follow.expire(now);
    Received rx;
    bool stopped = false;
    while (radio.poll(rx)) {
      if (rx.tag == RIDE_RATE_TAG && receivedRate && phase(now) == Phase::Ride &&
          follow.fromSource(rx.mac, rx.rate.boot, rx.rate.run, now) &&
          (receivedRate->boot != rx.rate.boot || receivedRate->run != rx.rate.run ||
           newer(rx.rate.revision, receivedRate->revision))) {
        *receivedRate = rx.rate;
      }
      if (rx.tag != TAG) continue;
      if (!follow.accept(rx.packet, rx.mac, now)) continue;
      if (rx.packet.phase == Phase::Off && rx.packet.run && rx.packet.run != lastStop_) {
        lastStop_ = rx.packet.run;
        stopped = true;
      }
    }
    if (follow.packet().phase == Phase::Off || suppressedRun_ != follow.packet().run) suppressedRun_ = 0;
    return stopped;
  }

  void suppress() { suppressedRun_ = follow.packet().run; }
  bool assigned() const { return unit_ < Unit::Count; }
  uint8_t localId() const { return assigned() ? uint8_t(unit_) - uint8_t(first_) + 1 : 0; }
  bool idle(uint32_t now) const { return !follow.linked(now) || follow.packet().phase == Phase::Off; }
  Unit unit() const { return unit_; }
  Phase phase(uint32_t now) const {
    return assigned() && (!suppressedRun_ || suppressedRun_ != follow.packet().run)
        ? follow.phase(now) : Phase::Off;
  }
  bool muted(uint32_t now) const {
    return isActive(phase(now)) && (follow.packet().muted & unitLight(unit_));
  }

  void report(uint32_t now, uint8_t flags, uint8_t a = 0, uint8_t b = 0) {
    if (!assigned() || now - lastReport_ < 140U + uint8_t(unit_) * 3U) return;
    lastReport_ = now;
    Packet p;
    p.kind = Kind::Report;
    p.unit = unit_;
    p.run = follow.linked(now) ? follow.packet().run : 0;
    p.phase = phase(now);
    p.flags = suppressedRun_ ? (flags & Fault) : flags;
    p.a = a;
    p.b = b;
    p.muted = muted(now) ? unitLight(unit_) : 0;
    radio.send(p);
  }

  Radio radio;
  Follower follow;

 private:
  Unit first_ = Unit::Count, last_ = Unit::Count, unit_ = Unit::Count;
  uint32_t lastReport_ = 0, suppressedRun_ = 0, lastStop_ = 0;
};
}

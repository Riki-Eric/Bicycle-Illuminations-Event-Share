#pragma once
#include <stdint.h>

namespace OpeningShow {
enum class Phase : uint8_t { Off, Armed, Trail, Ride, White, Sparkle, Drape, Hold, ShortHold, Fade, TreeCharge, Steady };
enum class Unit : uint8_t { Bike, Mayor, Wing, TreeLower, TreeUpper, DrapeA, DrapeB, Plaza1, Plaza2, Plaza3, Count };
enum class Kind : uint8_t { Cue, Report };
enum Flag : uint8_t { Ready = 1, Pressed = 2, DrapeSent = 4, Fault = 8, Short = 16, ThreePlazas = 32 };

constexpr uint32_t TAG = 0x34544254; // Cues also carry individual blackout requests.
constexpr uint16_t RADIO_MS = 80, LINK_MS = 1500;
constexpr uint16_t TRAIL_MS = 3600, WHITE_MS = 1500, SPARKLE_MS = 5000;
constexpr uint16_t SHORT_HOLD_MS = 7000, FADE_MS = 3000;
constexpr uint8_t UNIT_COUNT = static_cast<uint8_t>(Unit::Count);
constexpr uint16_t MIN_PHASE_MS = 300, MAX_PHASE_MS = 60000;
constexpr uint16_t PEER_LIGHTS = 0x03fc; // Wing through Plaza3 use their Unit bit.

constexpr uint16_t unitLight(Unit unit) {
  return unit >= Unit::Wing && unit < Unit::Count ? uint16_t(1U << uint8_t(unit)) : 0;
}

constexpr bool timed(Phase p) {
  return p == Phase::Trail || p == Phase::White || p == Phase::Sparkle ||
         p == Phase::ShortHold || p == Phase::Fade;
}

struct Timings {
  uint16_t trail = TRAIL_MS, white = WHITE_MS, sparkle = SPARKLE_MS;
  uint16_t shortHold = SHORT_HOLD_MS, fade = FADE_MS;

  constexpr uint16_t duration(Phase p) const {
    switch (p) {
      case Phase::Trail: return trail;
      case Phase::White: return white;
      case Phase::Sparkle: return sparkle;
      case Phase::ShortHold: return shortHold;
      case Phase::Fade: return fade;
      default: return 0;
    }
  }
};

struct Packet {
  uint32_t tag = TAG;
  uint32_t boot = 0;
  uint32_t run = 0;
  uint32_t seq = 0;
  uint32_t elapsed = 0;
  Kind kind = Kind::Cue;
  Unit unit = Unit::Bike;
  Phase phase = Phase::Off;
  uint8_t flags = 0;
  uint8_t a = 0, b = 0;
  uint16_t duration = 0;
  uint16_t muted = 0;
  uint16_t reserved = 0;
};
static_assert(sizeof(Packet) == 32);

constexpr bool isDrape(Unit unit) { return unit == Unit::DrapeA || unit == Unit::DrapeB; }
constexpr bool isActive(Phase phase) { return phase >= Phase::Trail; }
constexpr uint8_t phaseOrder(Phase phase) {
  if (phase == Phase::TreeCharge) return 4;
  if (phase == Phase::Steady) return 11;
  return uint8_t(phase) + (phase >= Phase::White ? 1 : 0);
}
constexpr bool newer(uint32_t seq, uint32_t last) { return int32_t(seq - last) > 0; }

constexpr bool valid(const Packet &p) {
  return p.tag == TAG && p.boot && p.seq && p.a <= 100 && p.b <= 100 &&
      (p.phase == Phase::TreeCharge ? p.reserved <= 100 : p.reserved == 0) &&
      p.phase <= Phase::Steady &&
      ((p.kind == Kind::Cue && p.unit == Unit::Bike && !(p.flags & ~(Short | ThreePlazas)) &&
        (timed(p.phase) ? p.duration >= MIN_PHASE_MS && p.duration <= MAX_PHASE_MS : p.duration == 0) &&
        !(p.muted & ~PEER_LIGHTS) && (isActive(p.phase) || !p.muted) &&
        (p.run || p.phase == Phase::Off)) ||
       (p.kind == Kind::Report && p.unit > Unit::Bike && p.unit < Unit::Count &&
        !p.reserved && !p.duration && !(p.flags & ~(Ready | Pressed | DrapeSent | Fault)) &&
        (!(p.flags & Pressed) || p.unit == Unit::Mayor) &&
        (!(p.flags & DrapeSent) || isDrape(p.unit)) && !(p.muted & ~unitLight(p.unit))));
}

// A running show cannot be joined after a reboot or resumed on link recovery.
class Follower {
 public:
  constexpr bool accept(const Packet &p, const uint8_t *mac, uint32_t now) {
    if (!valid(p) || p.kind != Kind::Cue) return false;
    bool sameSource = cue_.boot == p.boot;
    for (uint8_t i = 0; i < 6; ++i) sameSource &= mac_[i] == mac[i];
    if (!sameSource && linked(now) && cue_.phase != Phase::Off) return false;
    if (sameSource && !newer(p.seq, cue_.seq)) return false;
    if (isActive(p.phase) && (!sameSource || !admitted_ || p.run != cue_.run ||
        phaseOrder(p.phase) < phaseOrder(cue_.phase) || !linked(now))) return false;
    if (p.phase == Phase::Armed) {
      if (sameSource && p.run == cue_.run && cue_.phase != Phase::Armed) return false;
      admitted_ = true;
    } else if (p.phase == Phase::Off) {
      admitted_ = false;
    }
    cue_ = p;
    for (uint8_t i = 0; i < 6; ++i) mac_[i] = mac[i];
    seenAt_ = now;
    return true;
  }

  constexpr bool linked(uint32_t now) const { return cue_.boot && now - seenAt_ < LINK_MS; }
  constexpr bool fromSource(const uint8_t *mac, uint32_t boot, uint32_t run, uint32_t now) const {
    if (!linked(now) || boot != cue_.boot || run != cue_.run) return false;
    for (uint8_t i = 0; i < 6; ++i) if (mac_[i] != mac[i]) return false;
    return true;
  }
  constexpr void expire(uint32_t now) {
    if (!linked(now) && isActive(cue_.phase)) admitted_ = false;
  }
  constexpr Phase phase(uint32_t now) const { return linked(now) && admitted_ ? cue_.phase : Phase::Off; }
  constexpr uint32_t elapsed(uint32_t now) const { return cue_.elapsed + (now - seenAt_); }
  constexpr const Packet &packet() const { return cue_; }

 private:
  Packet cue_{};
  uint8_t mac_[6]{};
  uint32_t seenAt_ = 0;
  bool admitted_ = false;
};

class Sequence {
 public:
  constexpr void enter(Phase phase, uint32_t now) { phase_ = phase; started_ = now; }
  constexpr Phase phase() const { return phase_; }
  constexpr uint32_t elapsed(uint32_t now) const { return now - started_; }
  constexpr Phase backupNext(bool shortShow) const {
    switch (phase_) {
      case Phase::Trail: return Phase::Ride;
      case Phase::Ride: return shortShow ? Phase::ShortHold : Phase::TreeCharge;
      case Phase::TreeCharge: return Phase::White;
      case Phase::White: return Phase::Sparkle;
      case Phase::Sparkle: return Phase::Drape;
      case Phase::Drape: return Phase::Hold;
      case Phase::Hold: return Phase::Steady;
      case Phase::Steady: return Phase::Armed;
      case Phase::ShortHold: return Phase::Fade;
      default: return phase_;
    }
  }
  constexpr Phase next(uint32_t now, bool shortShow, bool bothGoals, bool bothDrapes,
                       const Timings &timings = {}, bool treeGoal = false) const {
    const uint32_t age = elapsed(now);
    switch (phase_) {
      case Phase::Trail: return age >= timings.trail ? Phase::Ride : phase_;
      case Phase::Ride: return bothGoals ? (shortShow ? Phase::ShortHold : Phase::TreeCharge) : phase_;
      case Phase::TreeCharge: return treeGoal ? Phase::White : phase_;
      case Phase::White: return age >= timings.white ? Phase::Sparkle : phase_;
      case Phase::Sparkle: return age >= timings.sparkle ? Phase::Drape : phase_;
      case Phase::Drape: return bothDrapes ? Phase::Hold : phase_;
      case Phase::ShortHold: return age >= timings.shortHold ? Phase::Fade : phase_;
      case Phase::Fade: return age >= timings.fade ? Phase::Off : phase_;
      default: return phase_;
    }
  }

 private:
  Phase phase_ = Phase::Off;
  uint32_t started_ = 0;
};
}

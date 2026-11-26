#pragma once
#include "BikeConfig.h"
#include "SessionLogic.h"

// One descriptor per wing. Keep the last rendered pattern for photo and fade.
struct StormLayer {
  uint8_t style;
  uint16_t age;
  uint8_t speed;
};

class StormEffects {
 public:
  void update(const RideView &a, const RideView &b, uint8_t style) {
    if (style != style_) {
      wings_[0] = {};
      wings_[1] = {};
      duet_ = Duet::Waiting;
      style_ = style;
    }
    if (style < BikeConfig::SPEED_STYLE_BURST) return;
    if (style == BikeConfig::SPEED_STYLE_FINALE || style == BikeConfig::SPEED_STYLE_AUTO)
      updateDuet(a, b);
    updateWing(wings_[0], a);
    updateWing(wings_[1], b);
  }

  bool active(uint8_t side) const { return wings_[side].layer.style != 0; }

  uint8_t highlight(uint8_t side, uint8_t depth, uint8_t span, bool &gold) const {
    const StormLayer &layer = wings_[side].layer;
    switch (layer.style) {
      case BikeConfig::SPEED_STYLE_BURST:
        return waves(layer.age, depth, span, 3, 240, 440, 8);
      case BikeConfig::SPEED_STYLE_LIGHTNING: {
        const uint8_t width = 2 + (uint16_t(layer.speed - LIGHTNING_LEVEL) * 3) / 80;
        const uint8_t a = comet(layer.age, depth, span, 260, width);
        const uint8_t b = comet((layer.age + 210) % 480, depth, span, 290, width + 1);
        return a > b ? a : b;
      }
      case BikeConfig::SPEED_STYLE_FIREWORKS:
        return fireworks(layer.age, depth, span, gold);
      case BikeConfig::SPEED_STYLE_FINALE:
        gold = (layer.age / 220) % 2;
        return waves(layer.age, depth, span, 5, 220, 750, 16);
      default:
        return 0;
    }
  }

 private:
  struct Wing {
    StormLayer layer = {};
    uint32_t burstAt = 0;
    uint32_t fireworksAt = 0;
    uint8_t previousSpeed = 0;
    bool burst = false;
    bool fireworks = false;
  };
  enum class Duet : uint8_t { Waiting, Qualifying, Playing, Latched };
  static constexpr uint8_t LIGHTNING_LEVEL = 175;
  static constexpr uint8_t FIREWORK_LEVEL = 185;
  static constexpr uint8_t DUET_LEVEL = 215;
  static constexpr uint16_t RECENT_PULSE_MS = 1000;
  static constexpr uint16_t AUTO_FIREWORK_WAIT_MS = 1600;
  static constexpr uint16_t AUTO_FIREWORK_CYCLE_MS = 4200;
  static constexpr uint16_t FIREWORK_TRAVEL_MS = 480;
  static constexpr uint16_t FIREWORK_BLOOM_MS = 580;
  static constexpr uint16_t FIREWORK_DURATION_MS = FIREWORK_TRAVEL_MS + FIREWORK_BLOOM_MS;
  static constexpr uint16_t DUET_HOLD_MS = 2500;
  static constexpr uint16_t DUET_DURATION_MS = 1630;

  void updateDuet(const RideView &a, const RideView &b) {
    const bool bothFast = a.phase == RidePhase::Riding && b.phase == RidePhase::Riding &&
                          a.speedLevel >= DUET_LEVEL && b.speedLevel >= DUET_LEVEL &&
                          a.pulseAgeMs < RECENT_PULSE_MS && b.pulseAgeMs < RECENT_PULSE_MS;
    const uint32_t now = a.visualTimeMs;
    if (duet_ == Duet::Playing && a.phase != RidePhase::Riding && b.phase != RidePhase::Riding)
      duet_ = Duet::Waiting;
    if (duet_ == Duet::Playing) {
      // One wing can stop receiving input before the other. The moving wing's
      // clock still advances while the stopped wing keeps its last frame.
      const uint32_t liveNow = a.phase == RidePhase::Riding ? now : b.visualTimeMs;
      if (uint32_t(liveNow - duetAt_) >= DUET_DURATION_MS)
        duet_ = bothFast ? Duet::Latched : Duet::Waiting;
    } else if (!bothFast) {
      duet_ = Duet::Waiting;
    } else if (duet_ == Duet::Waiting) {
      duet_ = Duet::Qualifying;
      duetAt_ = now;
    } else if (duet_ == Duet::Qualifying && uint32_t(now - duetAt_) >= DUET_HOLD_MS) {
      duet_ = Duet::Playing;
      duetAt_ = now;
    }
  }

  void updateWing(Wing &wing, const RideView &view) {
    if (view.phase == RidePhase::Idle) wing = {};
    if (view.phase != RidePhase::Riding) {
      wing.previousSpeed = 0;
      wing.burst = wing.fireworks = false;
      return;
    }
    const uint32_t now = view.visualTimeMs;
    const uint8_t speed = view.speedLevel;
    const bool automatic = style_ == BikeConfig::SPEED_STYLE_AUTO;
    const bool recent = view.pulseAgeMs < RECENT_PULSE_MS;
    wing.layer = {};
    // More distinct cues replace the narrow lightning streaks while they play.
    // Continue tracking the local timings even when the duet takes both wings.
    if ((style_ == BikeConfig::SPEED_STYLE_LIGHTNING || (automatic && recent)) &&
        speed >= LIGHTNING_LEVEL)
      wing.layer = {BikeConfig::SPEED_STYLE_LIGHTNING, uint16_t(now % 480), speed};
    if (automatic || style_ == BikeConfig::SPEED_STYLE_FIREWORKS)
      updateFireworks(wing, now, speed >= FIREWORK_LEVEL && (!automatic || recent), automatic);
    if (automatic || style_ == BikeConfig::SPEED_STYLE_BURST)
      updateBurst(wing, now, speed);
    if (duet_ == Duet::Playing)
      wing.layer = {BikeConfig::SPEED_STYLE_FINALE, uint16_t(now - duetAt_), 0};
    wing.previousSpeed = speed;
  }

  static void updateBurst(Wing &wing, uint32_t now, uint8_t speed) {
    if (wing.burst && uint32_t(now - wing.burstAt) >= 1400) wing.burst = false;
    if (!wing.burst && wing.previousSpeed && speed >= 145 &&
        speed >= uint16_t(wing.previousSpeed) + 26) {
      wing.burst = true;
      wing.burstAt = now;
    }
    if (wing.burst && uint32_t(now - wing.burstAt) < 920)
      wing.layer = {BikeConfig::SPEED_STYLE_BURST, uint16_t(now - wing.burstAt), 0};
  }

  static void updateFireworks(Wing &wing, uint32_t now, bool fast, bool automatic) {
    const uint16_t wait = automatic ? AUTO_FIREWORK_WAIT_MS : 0;
    const uint16_t cycle = automatic ? AUTO_FIREWORK_CYCLE_MS : 1420;
    if (!wing.fireworks && fast) {
      wing.fireworks = true;
      wing.fireworksAt = now;
    }
    if (!wing.fireworks) return;
    const uint32_t elapsed = now - wing.fireworksAt;
    if (elapsed >= cycle || (automatic && !fast && elapsed < wait)) {
      wing.fireworks = false;
    } else if (elapsed >= wait && elapsed < wait + FIREWORK_DURATION_MS) {
      wing.layer = {BikeConfig::SPEED_STYLE_FIREWORKS, uint16_t(elapsed - wait), 0};
    }
  }

  static uint8_t comet(uint16_t age, uint8_t depth, uint8_t span, uint16_t duration, uint8_t tail) {
    if (age >= duration) return 0;
    const uint8_t head = uint32_t(age) * (span + tail) / duration;
    if (depth > head || head - depth >= tail) return 0;
    return 255 - uint16_t(head - depth) * 255 / tail;
  }

  static uint8_t waves(uint16_t age, uint8_t depth, uint8_t span, uint8_t count,
                       uint16_t gap, uint16_t duration, uint8_t tail) {
    uint8_t brightest = 0;
    for (uint8_t i = 0; i < count && age >= i * gap; ++i) {
      const uint8_t light = comet(age - i * gap, depth, span, duration, tail);
      if (light > brightest) brightest = light;
    }
    return brightest;
  }

  static uint8_t fireworks(uint16_t age, uint8_t depth, uint8_t span, bool &gold) {
    if (age < FIREWORK_TRAVEL_MS) return comet(age, depth, span, FIREWORK_TRAVEL_MS, 6);
    age -= FIREWORK_TRAVEL_MS;
    if (age >= FIREWORK_BLOOM_MS) return 0;
    const uint8_t fromTip = span - depth;
    const uint8_t radius = 1 + uint32_t(age) * (span / 3 + 1) / FIREWORK_BLOOM_MS;
    if (fromTip >= radius) return 0;
    const uint8_t glow = 255 - uint32_t(age) * 255 / FIREWORK_BLOOM_MS;
    gold = fromTip != 0;
    return uint16_t(glow) * (radius - fromTip) / radius;
  }

  Wing wings_[2];
  Duet duet_ = Duet::Waiting;
  uint8_t style_ = 0;
  uint32_t duetAt_ = 0;
};

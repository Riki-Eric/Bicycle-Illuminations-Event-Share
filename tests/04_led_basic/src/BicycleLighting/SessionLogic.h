#pragma once
#include <stdint.h>

enum class ExhibitMode : uint8_t { Speed, Accumulation };
enum class RidePhase : uint8_t { Idle, Riding, Celebration, Photo, Fade };
struct RideDifficulty {
  uint16_t shotRevolutions, targetRevolutions;
};
struct SessionSettings {
  uint16_t referencePulses, targetRevolutions, fullEffectRpm;
  uint32_t stopMs, celebrationMs, photoMs, fadeMs;
  uint8_t referenceTurns;
  bool animatePhoto;
  uint16_t resumeMs, finishMs; // Nonzero finish enables automatic clearing after the last pulse.
  uint16_t shotRevolutions;

  uint32_t targetPulses() const {
    return (uint32_t(referencePulses) * targetRevolutions + referenceTurns - 1) / referenceTurns;
  }
};
struct RideView {
  RidePhase phase;
  uint32_t totalPulses, visualTimeMs, pulseAgeMs;
  uint8_t speedLevel, progress, alpha, sessionId;
  bool goalReached;
  RideDifficulty difficulty;
};
inline uint16_t centerOutIndex(uint16_t total, uint8_t side, uint16_t depth) {
  const uint16_t half = total / 2;
  return side == 0 ? half - 1 - depth : half + depth;
}

// Foreground session state. Sensor lifetime counters are not reset here.
class RiderSession {
 public:
  explicit RiderSession(SessionSettings settings)
      : cfg_(settings), nextDifficulty_{settings.shotRevolutions, settings.targetRevolutions} {}
  void setNextDifficulty(RideDifficulty difficulty) { nextDifficulty_ = difficulty; }
  bool hasNewPulse(uint32_t counter) const { return counter != previousCounter_; }
  bool expired(uint32_t now) const {
    return cfg_.finishMs && total_ && uint32_t(now - lastPulseAt_) >= cfg_.finishMs;
  }
  RideView update(uint32_t now, uint32_t counter, uint32_t periodUs,
                  ExhibitMode mode, bool resetHeld, bool acceptInput = true,
                  bool keepProgress = false, bool repeatAfterGoal = false) {
    // Keep the raw baseline current even while this rider is not participating.
    uint32_t delta = acceptInput ? counter - previousCounter_ : 0;
    previousCounter_ = counter;
    if (resetHeld) {
      clearRide();
      mode_ = mode;
      return view(now);
    }
    if (keepProgress) {
      if (goalReached() && !repeatAfterGoal) delta = 0;
    } else {
      if (expired(now)) clearRide();
      else if (cfg_.finishMs && total_ && uint32_t(now - lastPulseAt_) >= cfg_.resumeMs)
        delta = 0; // Late input must not extend the display or carry into the next ride.
    }
    if (delta) {
      if (!total_) {
        cfg_.shotRevolutions = nextDifficulty_.shotRevolutions;
        cfg_.targetRevolutions = nextDifficulty_.targetRevolutions;
      }
      total_ = UINT32_MAX - total_ < delta ? UINT32_MAX : total_ + delta;
      lastPulseAt_ = now;
      const uint32_t rpm = periodUs
          ? (60000000UL / periodUs * cfg_.referenceTurns / cfg_.referencePulses) : 0;
      uint8_t desired = rpm >= cfg_.fullEffectRpm ? 255 : uint8_t(rpm * 255UL / cfg_.fullEffectRpm);
      if (desired < 32) desired = 32;
      speed_ = phase_ == RidePhase::Idle ? desired : uint8_t((uint16_t(speed_) + desired * 3U) / 4U);
      if (phase_ == RidePhase::Idle || phase_ == RidePhase::Photo || phase_ == RidePhase::Fade)
        phase_ = RidePhase::Riding;
    }
    if (mode != mode_) {
      mode_ = mode;
      if (mode == ExhibitMode::Speed && phase_ == RidePhase::Celebration) phase_ = RidePhase::Riding;
    }
    if (!keepProgress && cfg_.finishMs && total_ && uint32_t(now - lastPulseAt_) >= cfg_.finishMs - cfg_.fadeMs) {
      phase_ = RidePhase::Fade;
      phaseAt_ = lastPulseAt_ + (cfg_.finishMs - cfg_.fadeMs);
    } else if (phase_ == RidePhase::Riding) {
      if (mode == ExhibitMode::Accumulation && goalReached() && !goalCelebrated_) {
        goalCelebrated_ = true; phase_ = RidePhase::Celebration; phaseAt_ = now;
      } else if (uint32_t(now - lastPulseAt_) >= cfg_.stopMs) enterPhoto(now);
    } else if (phase_ == RidePhase::Celebration && uint32_t(now - phaseAt_) >= cfg_.celebrationMs) {
      if (uint32_t(now - lastPulseAt_) < cfg_.stopMs) phase_ = RidePhase::Riding;
      else enterPhoto(now);
    } else if (!cfg_.finishMs && phase_ == RidePhase::Photo && uint32_t(now - phaseAt_) >= cfg_.photoMs) {
      phase_ = RidePhase::Fade; phaseAt_ = now;
    } else if (phase_ == RidePhase::Fade && uint32_t(now - phaseAt_) >= cfg_.fadeMs) phase_ = RidePhase::Idle;
    return view(now);
  }
 private:
  void clearRide() {
    if (total_) ++sessionId_;
    total_ = 0; speed_ = 0; phase_ = RidePhase::Idle;
    goalCelebrated_ = false;
  }
  uint32_t target() const { return cfg_.targetPulses(); }
  bool goalReached() const { return total_ >= target(); }
  void enterPhoto(uint32_t now) { phase_ = RidePhase::Photo; phaseAt_ = frozenAt_ = now; }
  RideView view(uint32_t now) const {
    const uint8_t progress = goalReached() ? 255 : uint8_t(total_ * 255UL / target());
    uint8_t alpha = phase_ == RidePhase::Idle ? 0 : 255;
    if (phase_ == RidePhase::Fade) {
      const uint32_t elapsed = now - phaseAt_;
      alpha = elapsed >= cfg_.fadeMs ? 0 : uint8_t(255UL - elapsed * 255UL / cfg_.fadeMs);
    }
    const bool holdFrame = !cfg_.animatePhoto && (phase_ == RidePhase::Photo || phase_ == RidePhase::Fade);
    return {phase_, total_, holdFrame ? frozenAt_ : now,
            now - lastPulseAt_, speed_, progress, alpha, sessionId_, goalReached(),
            {cfg_.shotRevolutions, cfg_.targetRevolutions}};
  }
  SessionSettings cfg_;
  RideDifficulty nextDifficulty_;
  ExhibitMode mode_ = ExhibitMode::Speed;
  RidePhase phase_ = RidePhase::Idle;
  uint32_t previousCounter_ = 0, total_ = 0, lastPulseAt_ = 0, phaseAt_ = 0, frozenAt_ = 0;
  uint8_t speed_ = 0, sessionId_ = 0;
  bool goalCelebrated_ = false;
};

class DebouncedContact {
 public:
  void begin(bool pressed, uint32_t now) { stable_ = candidate_ = pressed; changedAt_ = now; }
  bool update(bool pressed, uint32_t now, uint32_t debounceMs) {
    if (pressed != candidate_) { candidate_ = pressed; changedAt_ = now; }
    if (candidate_ != stable_ && uint32_t(now - changedAt_) >= debounceMs) stable_ = candidate_;
    return stable_;
  }
 private:
  bool stable_ = false, candidate_ = false;
  uint32_t changedAt_ = 0;
};

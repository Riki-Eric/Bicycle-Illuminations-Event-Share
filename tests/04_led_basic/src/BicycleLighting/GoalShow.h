#pragma once
#include <Arduino.h>
#include "BikeConfig.h"
#include "SessionLogic.h"

enum class GoalSeries : uint8_t { Standard, Tbt };

class GoalShow {
 public:
  struct Color {
    uint16_t hue;
    uint8_t saturation, value;
  };

  void setPreviewPattern(uint8_t pattern) { previewPattern_ = pattern; }
  void setSeries(GoalSeries series) { series_ = series; }

  static uint8_t patternCount(GoalSeries series) {
    return series == GoalSeries::Tbt ? BikeConfig::TBT_PATTERN_COUNT : BikeConfig::GOAL_PATTERN_COUNT;
  }
  static uint8_t rarePattern(GoalSeries series) {
    return series == GoalSeries::Tbt ? BikeConfig::TBT_RAREST_PATTERN : BikeConfig::GOAL_RAREST_PATTERN;
  }

  void update(uint8_t side, const RideView &view, bool accumulation,
              bool drawGoal = true, bool repeatAfterGoal = false) {
    Wing &wing = wings_[side];
    if (wing.sessionId != view.sessionId) {
      wing = Wing{};
      wing.sessionId = view.sessionId;
    }
    if (!view.totalPulses) {
      return;
    }
    if (!accumulation) {
      wing.flying = false;
      return;
    }
    const uint16_t calPulses = side ? BikeConfig::TIRE_CAL_PULSES_B : BikeConfig::TIRE_CAL_PULSES_A;
    const uint8_t calTurns = side ? BikeConfig::TIRE_CAL_TURNS_B : BikeConfig::TIRE_CAL_TURNS_A;
    const uint32_t shotUnits = uint32_t(view.difficulty.shotRevolutions) * calPulses;
    uint32_t earned;
    if (view.goalReached) {
      earned = goalShots(view.difficulty);
      if (repeatAfterGoal) {
        // Start a fresh shot interval at the goal, even if the final buildup shot was partial.
        const uint32_t goalPulses = (uint32_t(view.difficulty.targetRevolutions) * calPulses + calTurns - 1) / calTurns;
        const uint32_t extra = view.totalPulses - goalPulses;
        earned += min(extra, (UINT32_MAX - earned) / calTurns) * calTurns / shotUnits;
      }
    } else {
      earned = view.totalPulses * calTurns / shotUnits;
    }
    const uint32_t time = view.visualTimeMs;
    const bool active = view.phase == RidePhase::Riding || view.phase == RidePhase::Celebration;
    const bool newGoal = drawGoal && view.phase == RidePhase::Celebration && !wing.pattern;
    if (active && (earned > wing.earned || newGoal)) {
      wing.flightAt = time;
      wing.flying = true;
    }
    wing.earned = earned;
    if (wing.flying && time - wing.flightAt >= BikeConfig::GOAL_FLIGHT_MS) wing.flying = false;
    if (newGoal) {
      wing.series = series_;
      wing.pattern = choosePattern(wing.series);
      wing.announce = true;
    }
    if (wing.pattern && time - wing.flightAt >= BikeConfig::GOAL_FLIGHT_MS + 4200UL)
      wing.finale = true;
  }

  void logGoals() {
    for (uint8_t side = 0; side < 2; ++side) {
      Wing &wing = wings_[side];
      if (!wing.announce || Serial.availableForWrite() < 28) continue;
      Serial.print(side ? F("GOAL B id=") : F("GOAL A id="));
      Serial.print(wing.pattern);
      if (wing.series == GoalSeries::Tbt) Serial.print(F(" series=TBT"));
      Serial.println();
      wing.announce = false;
    }
  }

  struct Frame {
    uint16_t filled, head, hueShift;
    uint8_t pattern, phase, finale;
    GoalSeries series;
    bool flying;
  };

  Frame frame(uint8_t side, uint16_t half, const RideView &view) const {
    const Wing &wing = wings_[side];
    const uint32_t time = view.visualTimeMs;
    const uint16_t target = goalShots(view.difficulty);
    // A waiting wing keeps the full gauge lit under each additional shot.
    const uint16_t stored = wing.earned > target ? target : wing.earned - uint16_t(wing.flying);
    const uint16_t filled = (uint32_t(half) * stored + target - 1) / target;
    if (wing.flying) {
      const uint16_t head = (time - wing.flightAt) * 300UL / BikeConfig::GOAL_FLIGHT_MS;
      return {filled, head, 0, 0, 0, 0, wing.series, true};
    }
    if (!wing.pattern) {
      const bool afterglow = view.phase == RidePhase::Photo || view.phase == RidePhase::Fade;
      return {filled, 0, 0, 0, uint8_t(afterglow ? time / 12 : 0), 0, wing.series, false};
    }
    const uint32_t elapsed = time - wing.flightAt;
    const uint32_t age = elapsed >= BikeConfig::GOAL_FLIGHT_MS ? elapsed - BikeConfig::GOAL_FLIGHT_MS : 0;
    return {filled, 0, uint16_t(age * 13UL), wing.pattern, uint8_t(age / 5),
            uint8_t(wing.finale ? 2 : age >= 1800 ? 1 : 0), wing.series, false};
  }

  static Color color(const Frame &frame, uint8_t side, uint16_t depth, uint16_t half) {
    const uint8_t pos = half > 1 ? depth * 255U / (half - 1) : 0;
    if (frame.pattern) return frame.series == GoalSeries::Tbt
        ? tbtCelebration(frame, pos) : celebration(frame, pos);
    const uint16_t hue = side ? 6500 : 35000;
    if (frame.flying && frame.head >= pos && frame.head - pos < 52) {
      const uint8_t tail = frame.head - pos;
      return {hue, uint8_t(tail * 4), uint8_t(255 - tail * 2)};
    }
    if (half - depth <= frame.filled)
      return {hue, uint8_t(205 - pos / 3), uint8_t(110 + pos / 3 + wave(frame.phase) / 7)};
    return {hue, 255, 0};
  }

 private:
  struct Wing {
    uint32_t flightAt = 0;
    uint32_t earned = 0;
    uint8_t pattern = 0, sessionId = 0;
    GoalSeries series = GoalSeries::Standard;
    bool flying = false, finale = false, announce = false;
  };

  static uint16_t goalShots(RideDifficulty difficulty) {
    return (uint32_t(difficulty.targetRevolutions) + difficulty.shotRevolutions - 1) /
           difficulty.shotRevolutions;
  }

  uint8_t choosePattern(GoalSeries series) {
    const uint8_t count = patternCount(series);
    if (previewPattern_ && previewPattern_ <= count) return previewPattern_;
    if (!seeded_) {
      randomSeed(micros() | 1UL);
      seeded_ = true;
    }
    constexpr uint16_t standardTotal = BikeConfig::goalWeightSum(BikeConfig::GOAL_WEIGHTS, BikeConfig::GOAL_PATTERN_COUNT);
    constexpr uint16_t tbtTotal = BikeConfig::goalWeightSum(BikeConfig::TBT_WEIGHTS, BikeConfig::TBT_PATTERN_COUNT);
    const bool tbt = series == GoalSeries::Tbt;
    const uint8_t *weights = tbt ? BikeConfig::TBT_WEIGHTS : BikeConfig::GOAL_WEIGHTS;
    uint16_t ticket = random(tbt ? tbtTotal : standardTotal);
    for (uint8_t i = 0; i < count; ++i) {
      const uint8_t weight = weights[i];
      if (ticket < weight) return i + 1;
      ticket -= weight;
    }
    return count;
  }

  static uint8_t wave(uint8_t phase) {
    return phase < 128 ? phase * 2 : (255 - phase) * 2;
  }
  static uint8_t crest(uint8_t waveValue, uint8_t floor) {
    return waveValue > floor ? (waveValue - floor) * 255U / (254 - floor) : 0;
  }
  static Color lit(uint16_t hue, uint8_t light, uint8_t saturation = 235) {
    return {hue, uint8_t(saturation - uint16_t(saturation - 18) * light / 255),
            uint8_t(160 + uint16_t(light) * 95 / 255)};
  }
  static Color tbtCelebration(const Frame &frame, uint8_t pos) {
    switch (frame.pattern) {
      case 1: { // S-310 Akatsuki: Metro Blue and the first light of dawn.
        const uint8_t dawn = wave(frame.phase - pos / 2 + 24);
        const uint8_t edge = crest(dawn, 165);
        if (dawn > 165) return lit(2600 + uint16_t(edge) * 20, edge, 235);
        return {39000, 230, uint8_t(125 + dawn / 2)};
      }
      case 2: { // S-250 Sylphide: garnet ribbons carried by wind.
        const uint8_t ribbon = wave(frame.phase * 2 - pos * 2 - pos / 4);
        const uint8_t trailing = crest(wave(frame.phase * 2 - pos * 2 - pos / 4 + 80), 178);
        return lit(ribbon > 150 ? 64000 : 900, max(crest(ribbon, 150), uint8_t(trailing / 2)), 248);
      }
      case 3: { // S-290 Polaris: a fixed wingtip star and points that travel towards it.
        const uint8_t stars = crest(wave(frame.phase - pos * 3 - (pos / 43) * 19), 214);
        if (pos >= 231) {
          const uint8_t beacon = wave(frame.phase + 32);
          return {51000, uint8_t((255 - pos) * 3 + 18), uint8_t(205 + beacon / 6)};
        }
        if (stars) return lit(47900, stars, 100);
        return {47500, 235, uint8_t(88 + pos / 4)};
      }
      case 4: { // S-320 Element: two threads cross as they weave along the wing.
        const uint8_t threadA = crest(wave(frame.phase - pos * 2), 155);
        const uint8_t threadB = crest(wave(frame.phase * 2 + pos * 2 + 60), 155);
        if (threadA && threadB) return lit(25000, max(threadA, threadB), 75);
        if (threadB > threadA) return lit(18100, threadB, 240);
        return lit(32200, threadA, 238);
      }
      case 5: { // S-230 Cygnus: feather-like rows spread into a white-gold finale.
        const uint8_t feather = crest(wave(frame.phase - pos - (pos / 35) * 23), 150);
        const uint8_t shine = crest(wave(frame.phase + pos / 2 + 48), 150);
        if (frame.finale == 0) return lit(40500, feather, 130);
        if (frame.finale == 1) return lit(7400, max(feather, shine), 150);
        return lit(pos > 211 ? 6200 : 41000, max(max(feather, shine), uint8_t(110)), 95);
      }
      default: return {0, 0, 0};
    }
  }
  static Color celebration(const Frame &frame, uint8_t pos) {
    const uint8_t outward = wave(frame.phase - pos);
    const uint8_t inward = wave(frame.phase + pos + 64);
    const uint8_t bolt = crest(wave(frame.phase * 2 - pos - (pos / 40) * 9), 221);
    const uint8_t tipBurst = crest(wave(frame.phase - (255 - pos) / 2 + 48), 150);
    const uint8_t bloom = uint16_t(tipBurst) * pos / 255;
    const uint16_t rainbow = frame.hueShift - uint16_t(pos) * 260U;
    switch (frame.pattern) {
      case 1: {
        const uint8_t silver = crest(wave(frame.phase - pos + 84), 199);
        return lit(39500 - uint16_t(pos) * 37, max(max(bolt, silver), uint8_t(outward / 3)), 190);
      }
      case 2: return lit(3700 + uint16_t(pos) * 20, max(bloom, crest(outward, 215)));
      case 3: {
        const uint8_t rainbowCrest = outward / 2 + crest(outward, 194) / 2;
        return lit(pos > 214 ? 7900 : rainbow, max(bloom, rainbowCrest), 205);
      }
      case 4: {
        // Impact: the main front widens as it travels, followed by a weaker echo.
        const int16_t distance = int16_t(frame.phase) * 3 / 2 - pos;
        const uint8_t width = 24 + pos / 5;
        const uint8_t front = distance >= 0 && distance < width
            ? 255 - uint16_t(distance) * 255 / width : 0;
        const int16_t echoDistance = distance - 80;
        const uint8_t echo = echoDistance >= 0 && echoDistance < 24
            ? 120 - echoDistance * 5 : 0;
        return lit(49500 + uint16_t(pos) * 33, max(front, echo));
      }
      case 5: return outward >= inward ? lit(38200, crest(outward, 169))
                                      : lit(7350, crest(inward, 169));
      case 6: {
        // Comet: a white head moves ahead of a tail that fades from cyan to jade.
        const int16_t distance = int16_t(frame.phase) * 3 / 2 - pos;
        if (distance < 0 || distance >= 100) return {22000, 235, 105};
        if (distance < 9) return {31600, 20, 255};
        return {uint16_t(31600 - 105 * distance), 220,
                uint8_t(225 - uint16_t(distance) * 120 / 100)};
      }
      case BikeConfig::GOAL_RAREST_PATTERN:
        if (frame.finale == 0) return lit(39000, max(bolt, crest(outward, 165)), 150);
        if (frame.finale == 1) return lit(6700, max(bloom, crest(inward, 155)), 140);
        return lit(pos > 208 ? 7300 : 42000, max(max(bolt, bloom), uint8_t(85 + outward * 2U / 3U)), 115);
      case 8: {
        const uint8_t star = crest(wave(frame.phase - pos * 3 + 24), 225);
        if (star) return {7300, 180, uint8_t(210 + star / 6)};
        return lit(outward >= inward ? 500 : 22000, crest(max(outward, inward), 165) / 2);
      }
      default: return {0, 0, 0};
    }
  }

  Wing wings_[2];
  GoalSeries series_ = GoalSeries::Standard;
  uint8_t previewPattern_ = 0;
  bool seeded_ = false;
};

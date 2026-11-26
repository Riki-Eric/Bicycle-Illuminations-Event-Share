#pragma once
#include <Arduino.h>
#include "../Adafruit_NeoPixel/Adafruit_NeoPixel.h"
#include "BikeConfig.h"
#include "SessionLogic.h"
#include "StormEffects.h"
#include "GoalShow.h"

enum class FestivalLayout : uint8_t { Independent, SoloWaiting, SoloA, SoloB };
enum class ParticipationCue : uint8_t { None, Solo, Duo, Gap };
enum class MainShowStage : uint8_t { Off, Building, Finale, Finished };
constexpr uint16_t MAIN_FINALE_MS = 12000;

class StripOutputs {
 public:
  StripOutputs() : strip_(BikeConfig::LED_UNITS, BikeConfig::LED_PIN_A, BIKE_LED_TYPE) {}
  bool begin() {
    if (!strip_.getPixels() || !strip_.begin()) return false;
    strip_.setBrightness(BikeConfig::BRIGHTNESS);
    strip_.clear(); strip_.show();
    return true;
  }
  void setBrightness(uint8_t brightness) { strip_.setBrightness(brightness); }
  void setWingFlip(bool flipped) { flipped_ = flipped; }
  void setTrimUnits(uint8_t din, uint8_t far) {
    trim_[0] = din;
    trim_[1] = far;
    trimPreviewDrawn_ = false;
    drawnCue_ = ParticipationCue::None;
  }
  void setGoalPreviewPattern(uint8_t pattern) { goals_.setPreviewPattern(pattern); }
  void setGoalSeries(GoalSeries series) { goals_.setSeries(series); }
  void logGoals() { goals_.logGoals(); }
  static uint16_t outwardIndex(uint8_t side, uint16_t depth) {
    return centerOutIndex(BikeConfig::LED_UNITS, side, depth);
  }
  void showCenterMarker(bool visible) {
    strip_.clear();
    if (visible) {
      strip_.setPixelColor(outwardIndex(0, 0), strip_.Color(255, 0, 0));
      strip_.setPixelColor(outwardIndex(1, 0), strip_.Color(0, 0, 255));
    }
    strip_.show();
    black_ = !visible;
  }
  void basic(uint8_t test, uint16_t step) {
    strip_.clear();
    const uint16_t half = BikeConfig::LED_UNITS / 2;
    if (test >= 1 && test <= 4) {
      const uint32_t color = test == 1 ? strip_.Color(255, 0, 0) : test == 2 ? strip_.Color(0, 255, 0)
                               : test == 3 ? strip_.Color(0, 0, 255) : strip_.Color(255, 255, 255);
      strip_.fill(color);
    } else if (test == 5) {
      strip_.setPixelColor(outwardIndex(0, step % half), strip_.Color(0, 180, 255));
      strip_.setPixelColor(outwardIndex(1, step % half), strip_.Color(255, 100, 0));
    } else if (test == 6) {
      for (uint16_t d = 0; d < half; ++d) {
        strip_.setPixelColor(outwardIndex(0, d), strip_.Color(255, 0, 0));
        strip_.setPixelColor(outwardIndex(1, d), strip_.Color(0, 0, 255));
      }
    }
    strip_.show(); black_ = test == 0;
  }
  void render(const RideView &a, const RideView &b, ExhibitMode mode,
              uint8_t speedStyle, uint8_t goalStyle, bool trimPreview = false,
              FestivalLayout layout = FestivalLayout::Independent,
              ParticipationCue cue = ParticipationCue::None,
              MainShowStage main = MainShowStage::Off, uint32_t finaleAge = 0) {
    storm_.update(a, b, mode == ExhibitMode::Speed ? speedStyle : 0);
    if (goalStyle == BikeConfig::GOAL_STYLE_FESTIVAL &&
        (main == MainShowStage::Off || main == MainShowStage::Building)) {
      const bool accumulation = mode == ExhibitMode::Accumulation;
      const bool repeatAfterGoal = main == MainShowStage::Building;
      goals_.update(0, a, accumulation, !repeatAfterGoal, repeatAfterGoal);
      goals_.update(1, b, accumulation, !repeatAfterGoal, repeatAfterGoal);
    }
    if (cue != ParticipationCue::None) {
      trimPreviewDrawn_ = false;
      if (drawnCue_ != cue) showParticipationCue(cue);
      drawnCue_ = cue;
      return;
    }
    drawnCue_ = ParticipationCue::None;
    if (trimPreview) {
      if (!trimPreviewDrawn_) showTrimPreview();
      trimPreviewDrawn_ = true;
      return;
    }
    trimPreviewDrawn_ = false;
    const RideView *solo = layout == FestivalLayout::SoloA ? &a
                         : layout == FestivalLayout::SoloB ? &b : nullptr;
    const bool dark = main == MainShowStage::Finished || layout == FestivalLayout::SoloWaiting ||
                      (solo ? solo->alpha == 0 : a.alpha == 0 && b.alpha == 0);
    if (dark && black_) return;
    strip_.clear();
    if (!dark) {
      if (main == MainShowStage::Finale) {
        paintMainFinale(0, finaleAge);
        paintMainFinale(1, finaleAge);
      } else if (main == MainShowStage::Building) {
        if (a.alpha) paintFestival(0, 0, a, true);
        if (b.alpha) paintFestival(1, 1, b, true);
      } else if (solo) {
        const uint8_t rider = layout == FestivalLayout::SoloB;
        paintFestival(0, rider, *solo);
        paintFestival(1, rider, *solo);
      } else {
        paint(0, a, mode, speedStyle, goalStyle);
        paint(1, b, mode, speedStyle, goalStyle);
      }
    }
    strip_.show(); // AVR IRQ restriction: real sensor loss still requires testing.
    black_ = dark;
  }
 private:
  uint16_t span(uint8_t physicalSide) const { return BikeConfig::LED_UNITS / 2 - trim_[physicalSide]; }
  void showParticipationCue(ParticipationCue cue) {
    const bool dark = cue == ParticipationCue::Gap;
    if (dark && black_) return;
    strip_.clear();
    if (!dark) {
      const uint32_t color = cue == ParticipationCue::Solo ? strip_.Color(0, 160, 255)
                                                          : strip_.Color(255, 150, 0);
      strip_.fill(color, trim_[0], span(0) + span(1));
    }
    strip_.show();
    black_ = dark;
  }
  void showTrimPreview() {
    strip_.clear();
    for (uint8_t side = 0; side < 2; ++side) {
      const uint16_t length = span(side);
      const uint32_t color = side == 0 ? strip_.Color(0, 170, 255) : strip_.Color(255, 115, 0);
      for (uint16_t d = 0; d < length; ++d) strip_.setPixelColor(outwardIndex(side, d), color);
      strip_.setPixelColor(outwardIndex(side, length - 1), strip_.Color(255, 255, 255));
    }
    strip_.show();
    black_ = false;
  }
  void paint(uint8_t side, const RideView &v, ExhibitMode mode, uint8_t speedStyle, uint8_t goalStyle) {
    if (!v.alpha) return;
    if (mode == ExhibitMode::Accumulation && goalStyle == BikeConfig::GOAL_STYLE_FESTIVAL) {
      paintFestival(side, side, v);
      return;
    }
    if (mode == ExhibitMode::Speed && speedStyle >= BikeConfig::SPEED_STYLE_STORM) {
      paintStorm(side, v);
      return;
    }
    const uint16_t half = span(side ^ flipped_);
    const uint32_t time = v.visualTimeMs;
    for (uint16_t d = 0; d < half; ++d) {
      uint16_t hue = 0;
      uint8_t value = 0, saturation = 230;
      if (mode == ExhibitMode::Accumulation && v.goalReached) {
        // Smooth outward rainbow + gold flecks, intentionally no strobe.
        hue = uint16_t(time * 12UL - d * 1500UL + side * 10000UL);
        value = uint8_t(180 + strip_.sine8(uint8_t(time / 12 - d * 9)) / 4);
        if ((d + time / 120) % 11 == 0) { hue = 7000; saturation = 70; value = 255; }
      } else if (mode == ExhibitMode::Accumulation) {
        const uint16_t filled = (uint32_t(half) * v.progress + 254) / 255;
        if (d < filled) {
          hue = goalStyle == 0 ? uint16_t(6000 + d * 50) : uint16_t(time * 5UL - d * 1700UL);
          value = uint8_t(120 + strip_.sine8(uint8_t(time / 16 - d * 10)) / 2);
        }
      } else {
        const uint16_t extent = 1 + uint32_t(half - 1) * v.speedLevel / 255;
        hue = uint16_t((side ? 5000 : 35000) + time * (2 + v.speedLevel / 32) - d * 700UL);
        if (speedStyle == 0 && d < extent)
          value = uint8_t(70 + strip_.sine8(uint8_t(time / (24 - v.speedLevel / 16) - d * 15)) * 3U / 4U);
        else if (speedStyle == 1) {
          const uint16_t head = (time / (120 - v.speedLevel / 3)) % half;
          if (d <= head && head - d < 7) value = uint8_t(255 - (head - d) * 32);
        }
      }
      value = uint8_t(uint16_t(value) * v.alpha / 255);
      strip_.setPixelColor(outwardIndex(side ^ flipped_, d), strip_.ColorHSV(hue, saturation, value));
    }
  }
  void paintFestival(uint8_t side, uint8_t rider, const RideView &v, bool main = false) {
    const uint16_t half = span(side ^ flipped_);
    const GoalShow::Frame frame = goals_.frame(rider, half, v);
    for (uint16_t depth = 0; depth < half; ++depth) {
      GoalShow::Color color = GoalShow::color(frame, rider, depth, half);
      if (main) {
        // Four colors make the long build visible from across the venue.
        if (v.goalReached) color.hue = 7300;
        else if (v.progress >= 192) color.hue = 51000;
        else if (v.progress >= 128) color.hue = 31000;
        else if (v.progress >= 64) color.hue = 15000;
        if (depth == 0 && !color.value) color.value = 48;
      }
      const uint8_t value = uint16_t(color.value) * v.alpha / 255;
      strip_.setPixelColor(outwardIndex(side ^ flipped_, depth), strip_.ColorHSV(color.hue, color.saturation, value));
    }
  }
  void paintMainFinale(uint8_t side, uint32_t age) {
    const uint8_t physical = side ^ flipped_;
    const uint16_t half = span(physical);
    const uint8_t phase = age < 3000 ? 0 : age < 7000 ? 1 : 2;
    const GoalShow::Frame frame{0, 0, 0, BikeConfig::GOAL_RAREST_PATTERN,
                                uint8_t(age / 5), phase, GoalSeries::Standard, false};
    const uint32_t fadeAt = BikeConfig::FESTIVAL_FINISH_MS - BikeConfig::FADE_MS;
    for (uint16_t depth = 0; depth < half; ++depth) {
      const GoalShow::Color color = age < MAIN_FINALE_MS
          ? GoalShow::color(frame, 0, depth, half)
          : GoalShow::Color{7300, 180, uint8_t(145 + strip_.sine8(uint8_t(age / 14 - depth * 6)) / 3)};
      const uint8_t value = age < fadeAt ? color.value
          : uint32_t(color.value) * (BikeConfig::FESTIVAL_FINISH_MS - age) / BikeConfig::FADE_MS;
      strip_.setPixelColor(outwardIndex(physical, depth), strip_.ColorHSV(color.hue, color.saturation, value));
    }
  }
  void paintStorm(uint8_t side, const RideView &v) {
    const uint16_t half = span(side ^ flipped_);
    const uint16_t minReach = 1 + half / 2;
    const uint16_t reach = minReach + uint32_t(half + 3 - minReach) * v.speedLevel / 255;
    const uint8_t fastPhase = uint8_t(v.visualTimeMs / 4);
    const uint8_t slowPhase = uint8_t(v.visualTimeMs / 18);
    const uint8_t fastWeight = 48 + v.speedLevel * 3U / 4U;
    const uint8_t slowWeight = 160 - v.speedLevel / 3;
    const uint16_t hueBase = side == 0 ? 34000 : 8500;
    const bool overlay = storm_.active(side);
    for (uint16_t d = 0; d < half && (d < reach || overlay); ++d) {
      uint16_t hue = hueBase, value = 0;
      uint8_t saturation = 255;
      if (d < reach) {
        // Cadence changes the weight of the waves without jumping their positions.
        const uint8_t wave = strip_.sine8(uint8_t(fastPhase - d * 17));
        const uint8_t band = strip_.sine8(uint8_t(slowPhase - d * 9));
        const uint8_t crest = uint16_t(wave) * wave / 255;
        const uint16_t hueShift = uint16_t(band) * 60;
        hue = side == 0 ? hueBase + hueShift : hueBase - hueShift;
        const uint16_t highlight = uint16_t(crest) * fastWeight;
        saturation = 255 - highlight / 255;
        const uint8_t background = uint16_t(band) * slowWeight / 1024;
        value = 105 + background + highlight / 512;
        const uint16_t edge = reach - d;
        if (edge < 4) value = value * edge / 4;
      }
      if (overlay) {
        bool gold = false;
        const uint8_t light = storm_.highlight(side, d, half - 1, gold);
        if (light) {
          value += (255 - value) * light / 255;
          if (gold) hue = 7000;
          saturation -= uint16_t(saturation) * light / (gold ? 360 : 255);
        }
      }
      value = value * v.alpha / 255;
      strip_.setPixelColor(outwardIndex(side ^ flipped_, d), strip_.ColorHSV(hue, saturation, uint8_t(value)));
    }
  }
  Adafruit_NeoPixel strip_;
  StormEffects storm_;
  GoalShow goals_;
  uint8_t trim_[2] = {};
  ParticipationCue drawnCue_ = ParticipationCue::None;
  bool black_ = true, flipped_ = false, trimPreviewDrawn_ = false;
};

#pragma once

#include "DrapeShow.h"
#include "DrapeSpark.h"

namespace DrapeFinale {
constexpr uint16_t FRAME_MS = 20;
constexpr uint16_t SPARK_FADE_MS = 450;

struct Frame {
  uint16_t litCount;
  uint16_t headDistance;
  uint8_t sparkStep;
  uint8_t sparkStrength;
};

inline Frame frameAt(uint32_t elapsed, uint16_t pixels) {
  const uint16_t count = DrapeShow::countAt(elapsed, pixels);
  const uint32_t remaining = elapsed < DrapeShow::SWEEP_MS ? DrapeShow::SWEEP_MS - elapsed : 0;
  const uint8_t strength = remaining >= SPARK_FADE_MS ? 255 : remaining * 255 / SPARK_FADE_MS;
  return {count, uint16_t((count - 1) / 2), uint8_t(elapsed / DrapeSpark::STEP_MS), strength};
}

inline uint8_t mixWhite(uint8_t channel, uint8_t amount) {
  return channel + (uint16_t(255 - channel) * amount + 127) / 255;
}

inline uint32_t pixelGrb(uint16_t localPixel, uint16_t chainPixel, const Frame &frame, uint16_t pixels) {
  const uint16_t distance = DrapeShow::distanceFromTop(localPixel, pixels);
  const uint32_t gold = DrapeShow::pixelGrb(localPixel, frame.litCount, false, pixels);
  if (gold == 0 || frame.sparkStrength == 0) return gold;

  const uint16_t behind = frame.headDistance - distance;
  if (behind >= DrapeSpark::TAIL || !DrapeSpark::whiteSpark(behind, chainPixel, frame.sparkStep)) return gold;

  const uint8_t peak = behind < DrapeSpark::WHITE_HEAD ? 255 - 30 * behind : 160 - 5 * behind;
  const uint8_t amount = (uint16_t(peak) * frame.sparkStrength + 127) / 255;
  const uint8_t green = mixWhite((gold >> 16) & 0xff, amount);
  const uint8_t red = mixWhite((gold >> 8) & 0xff, amount);
  const uint8_t blue = mixWhite(gold & 0xff, amount);
  return (uint32_t(green) << 16) | (uint32_t(red) << 8) | blue;
}
}

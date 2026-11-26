#pragma once
#include <stdint.h>

namespace DrapeSpark {
constexpr uint8_t WHITE_HEAD = 4;
constexpr uint8_t TAIL = 24;
constexpr uint8_t STEP_MS = 70;

inline bool whiteSpark(uint8_t behindHead, uint16_t pixel, uint8_t step) {
  const uint8_t shimmer = (uint16_t(pixel) * 37 + uint16_t(step) * 29) % 23;
  return behindHead < WHITE_HEAD || (behindHead < 17 && shimmer < 2);
}
}

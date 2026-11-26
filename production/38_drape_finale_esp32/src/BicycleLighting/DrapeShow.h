#pragma once

#include <stdint.h>

namespace DrapeShow {
constexpr uint16_t PIXELS = 100;
constexpr uint32_t SWEEP_MS = 4200;
constexpr uint16_t FRAME_MS = 50;
constexpr uint32_t GOLD_GRB = 0x286004;
constexpr uint32_t TEST_GRB = 0x030600;

constexpr uint16_t countAt(uint32_t elapsed, uint16_t pixels = PIXELS) {
  return elapsed >= SWEEP_MS ? pixels : 1 + elapsed * pixels / SWEEP_MS;
}

inline uint16_t distanceFromTop(uint16_t pixel, uint16_t pixels = PIXELS) {
  const uint16_t half = pixels / 2;
  const uint16_t leftCenter = half - (pixels % 2 == 0);
  return pixel < half ? leftCenter - pixel : pixel - half;
}

inline uint32_t pixelGrb(uint16_t pixel, uint16_t count, bool dim = false, uint16_t pixels = PIXELS) {
  return 2 * distanceFromTop(pixel, pixels) < count ? (dim ? TEST_GRB : GOLD_GRB) : 0;
}
}

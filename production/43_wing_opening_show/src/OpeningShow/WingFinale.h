#pragma once
#include <stdint.h>

namespace OpeningShow {
namespace WingFinale {
constexpr uint32_t GOLD = 0xffb423;
constexpr uint16_t SHORT_MS = 900;

constexpr uint16_t difference(uint16_t a, uint16_t b) {
  return a > b ? a - b : b - a;
}

constexpr uint8_t glow(uint16_t distance, uint16_t width) {
  return distance < width ? uint32_t(width - distance) * 255 / width : 0;
}

constexpr uint32_t blend(uint32_t base, uint32_t light, uint8_t amount) {
  uint32_t result = 0;
  for (uint8_t shift = 0; shift <= 16; shift += 8) {
    const uint32_t a = (base >> shift) & 255;
    const uint32_t b = (light >> shift) & 255;
    result |= ((a * (255 - amount) + b * amount) / 255) << shift;
  }
  return result;
}

constexpr uint16_t position(uint16_t pixel, uint16_t span) {
  return span > 1 ? uint32_t(pixel) * 1000 / (span - 1) : 1000;
}

constexpr uint32_t mainPixel(uint16_t pixel, uint16_t span, uint32_t age, uint32_t duration) {
  if (pixel >= span) return 0;
  if (age >= duration) return GOLD;
  const uint16_t time = age * 1000 / duration;
  const uint16_t pos = position(pixel, span);
  constexpr uint16_t reachTip = 550;
  if (time < reachTip) {
    const uint16_t head = uint32_t(time) * 1000 / reachTip;
    const uint32_t base = pos <= head ? GOLD : 0x071d46;
    // A wide blue wake helps the audience follow the narrow white lightning.
    const uint8_t wake = pos <= head ? glow(head - pos, 275) : 0;
    const uint8_t core = glow(difference(head, pos), 105);
    return blend(blend(base, 0x4aa5ff, wake), 0xf4fbff, core);
  }
  // The tip opens into separate, stretching trails over a steady gold field.
  const uint16_t bloom = time - reachTip;
  const uint16_t spread = bloom < 290 ? bloom : 290;
  const uint8_t decay = glow(bloom, 450);
  const uint16_t positions[] = {1000, uint16_t(1000 - spread / 2), uint16_t(1000 - spread)};
  constexpr uint32_t colors[] = {0xfffff2, 0x73c8ff, 0xff6659};
  uint32_t color = GOLD;
  for (uint8_t i = 0; i < 3; ++i) {
    const uint8_t intensity = uint16_t(glow(difference(pos, positions[i]), 75)) * decay / 255;
    color = blend(color, colors[i], intensity);
  }
  return color;
}

constexpr uint32_t shortPixel(uint16_t pixel, uint16_t span, uint32_t age) {
  if (pixel >= span) return 0;
  if (age >= SHORT_MS) return GOLD;
  const uint16_t head = age * 1000 / SHORT_MS;
  return blend(GOLD, 0xe5faff, glow(difference(head, position(pixel, span)), 190));
}
}
}

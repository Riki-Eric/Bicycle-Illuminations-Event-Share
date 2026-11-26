#pragma once
#include <Arduino.h>
#include <esp32-hal-rmt.h>

namespace OpeningShow {
constexpr uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint32_t(r) << 16) | (uint16_t(g) << 8) | b;
}
constexpr uint32_t scale(uint32_t color, uint8_t brightness) {
  return rgb((color >> 16) * brightness / 255, ((color >> 8) & 255) * brightness / 255,
             (color & 255) * brightness / 255);
}
constexpr uint32_t swapRedGreen(uint32_t color) {
  return ((color & 0x00ff00) << 8) | ((color & 0xff0000) >> 8) | (color & 0xff);
}

template <uint16_t Pixels>
class Addressable {
 public:
  explicit Addressable(uint8_t data, bool rgbOrder, int8_t enable = -1)
      : data_(data), rgbOrder_(rgbOrder), enable_(enable) {}
  void setRgbOrder(bool rgbOrder) { rgbOrder_ = rgbOrder; }
  bool begin() {
    if (enable_ >= 0) {
      digitalWrite(enable_, LOW);
      pinMode(enable_, OUTPUT);
    }
    if (!rmtInit(data_, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 20000000) ||
        !rmtSetEOT(data_, LOW)) return false;
    return clear();
  }
  template <class Paint>
  bool draw(Paint paint) {
    if (enable_ >= 0) digitalWrite(enable_, HIGH);
    size_t index = 0;
    for (uint16_t pixel = 0; pixel < Pixels; ++pixel) {
      uint32_t color = paint(pixel);
      if (!rgbOrder_) color = swapRedGreen(color);
      for (uint32_t mask = 0x800000; mask; mask >>= 1) {
        const bool one = color & mask;
        auto &s = symbols_[index++];
        s.level0 = 1;
        s.duration0 = one ? 13 : 7;
        s.level1 = 0;
        s.duration1 = one ? 12 : 18;
      }
    }
    auto &latch = symbols_[index++];
    latch.level0 = latch.level1 = 0;
    latch.duration0 = 6000;
    latch.duration1 = 1;
    const bool sent = rmtWrite(data_, symbols_, index, 40);
    if (!sent && enable_ >= 0) digitalWrite(enable_, LOW);
    return sent;
  }
  bool clear() {
    const bool sent = draw([](uint16_t) { return uint32_t{0}; });
    if (enable_ >= 0) digitalWrite(enable_, LOW);
    return sent;
  }

 private:
  rmt_data_t symbols_[Pixels * 24 + 1]{};
  uint8_t data_;
  bool rgbOrder_;
  int8_t enable_;
};
}

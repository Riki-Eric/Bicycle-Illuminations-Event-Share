#pragma once
#include <Arduino.h>
#include "OpeningShow/Addressable.h"
#include "WingBridgeWire.h"

// Stage the same colors as the Rev E output and send one packet per wire interval.
class ArduinoWingOutput {
 public:
  ArduinoWingOutput(HardwareSerial &serial, uint8_t pin) : serial_(serial), pin_(pin) {}

  bool begin() {
    serial_.setTxBufferSize(512);
    serial_.begin(WingBridge::Baud, SERIAL_8N1, -1, pin_, true);
    lastSent_ = millis() - WingBridge::FrameMs;
    return bool(serial_);
  }

  void setRgbOrder(bool rgbOrder) { rgbOrder_ = rgbOrder; }
  bool clear() {
    blank_ = true;
    return true;
  }

  template <class PaintPixel>
  bool draw(PaintPixel paint) {
    for (uint16_t pixel = 0; pixel < WingBridge::Pixels; ++pixel) {
      uint32_t color = paint(pixel);
      if (!rgbOrder_) color = OpeningShow::swapRedGreen(color);
      frame_[3 + pixel * 3] = color >> 16;
      frame_[4 + pixel * 3] = color >> 8;
      frame_[5 + pixel * 3] = color;
    }
    blank_ = false;
    return true;
  }

  bool service(uint32_t now) {
    using namespace WingBridge;
    if (!serial_) return false;
    if (now - lastSent_ < FrameMs) return true;
    const uint16_t dataEnd = 3 + (blank_ ? 0 : ColorBytes);
    frame_[2] = blank_ ? Blank : Paint;
    uint16_t crc = 0xffff;
    for (uint16_t i = 2; i < dataEnd; ++i) crc = crcByte(crc, frame_[i]);
    frame_[dataEnd] = crc >> 8;
    frame_[dataEnd + 1] = crc;
    if (serial_.availableForWrite() < dataEnd + 2) return false;
    lastSent_ = now;
    return serial_.write(frame_, dataEnd + 2) == dataEnd + 2;
  }

 private:
  HardwareSerial &serial_;
  uint8_t frame_[WingBridge::ColorBytes + WingBridge::Overhead]{WingBridge::Sync1, WingBridge::Sync2};
  uint32_t lastSent_ = 0;
  const uint8_t pin_;
  bool rgbOrder_ = false;
  bool blank_ = true;
};

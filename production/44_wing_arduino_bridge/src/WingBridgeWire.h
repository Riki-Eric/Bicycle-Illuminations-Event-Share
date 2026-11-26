#pragma once
#include <stdint.h>

namespace WingBridge {
constexpr uint16_t Pixels = 100;
constexpr uint16_t ColorBytes = Pixels * 3;
constexpr uint32_t Baud = 38400;
constexpr uint16_t FrameMs = 100;
constexpr uint16_t GapMs = 10;
constexpr uint16_t TimeoutMs = 350;
constexpr uint8_t Sync1 = 0xa5, Sync2 = 0x5a;
enum Command : uint8_t { Blank = 0x31, Paint = 0x32 };
constexpr uint16_t Overhead = 5; // Two sync bytes, command, two CRC bytes.
static_assert((ColorBytes + Overhead) * 10000UL / Baud + GapMs < FrameMs,
              "Leave time for the AVR to latch LEDs and recover after a partial frame");

// CRC-16/CCITT-FALSE. The command and payload are covered, high byte first.
inline uint16_t crcByte(uint16_t crc, uint8_t value) {
  crc ^= uint16_t(value) << 8;
  for (uint8_t bit = 0; bit < 8; ++bit)
    crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
  return crc;
}
}

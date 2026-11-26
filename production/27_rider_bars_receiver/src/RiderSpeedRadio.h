#pragma once

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

namespace RiderSpeedRadio {
constexpr uint8_t CHANNEL = 6;
constexpr uint32_t PACKET_TAG = 0x01524942;  // BIR + version 1.

struct Packet {
  uint32_t tag;
  uint16_t rpmTenthsA;
  uint16_t rpmTenthsB;
};

static_assert(sizeof(Packet) == 8, "Unexpected RPM packet layout");

inline bool begin() {
  if (!WiFi.mode(WIFI_STA)) return false;
  WiFi.disconnect();
  WiFi.setSleep(false);
  return esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE) == ESP_OK &&
         esp_now_init() == ESP_OK;
}
}

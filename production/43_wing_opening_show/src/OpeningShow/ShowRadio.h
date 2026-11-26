#pragma once
#include "ShowState.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <freertos/queue.h>

namespace OpeningShow {
constexpr uint8_t CHANNEL = 6;
constexpr uint8_t BROADCAST[] = {255, 255, 255, 255, 255, 255};

constexpr uint32_t RIDE_SAMPLE_TAG = 0x31524454, RIDE_RATE_TAG = 0x32524454;
constexpr uint16_t RIDE_RATE_MIN = 50, RIDE_RATE_MAX = 200;
constexpr bool validRideRate(uint16_t value) { return value >= RIDE_RATE_MIN && value <= RIDE_RATE_MAX; }

struct RideSample {
  uint32_t tag = RIDE_SAMPLE_TAG, boot = 0, run = 0, revision = 0;
  uint32_t pulses[2]{}, goal[2]{};
  uint16_t rpm[2]{}, applied[2]{100, 100}, ppr[2]{}, baseRate[2]{100, 100};
};
struct RideRate {
  uint32_t tag = RIDE_RATE_TAG, boot = 0, run = 0, revision = 0;
  uint16_t value[2]{100, 100};
};
static_assert(sizeof(RideSample) == 48 && sizeof(RideRate) == 20);

struct Received {
  Packet packet;
  RideSample sample;
  RideRate rate;
  uint32_t tag = TAG;
  uint8_t mac[6]{};
};

class Radio {
 public:
  bool begin(const char *ssid = nullptr, const char *password = nullptr) {
    if (!WiFi.mode(ssid ? WIFI_AP_STA : WIFI_STA)) return false;
    WiFi.disconnect();
    WiFi.setSleep(false);
    if (ssid) {
      if (!WiFi.softAP(ssid, password, CHANNEL, false, 3)) return false;
    } else if (esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
      return false;
    }
    inbox_ = xQueueCreate(24, sizeof(Received));
    if (!inbox_ || esp_now_init() != ESP_OK) return false;
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, BROADCAST, sizeof(BROADCAST));
    peer.channel = CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    ready_ = esp_now_add_peer(&peer) == ESP_OK && esp_now_register_recv_cb(receive) == ESP_OK;
    boot_ = esp_random() | 1U;
    return ready_;
  }

  bool ready() const { return ready_; }
  uint32_t boot() const { return boot_; }
  bool poll(Received &message) { return inbox_ && xQueueReceive(inbox_, &message, 0) == pdPASS; }
  bool send(Packet p) {
    p.boot = boot_;
    if (++seq_ == 0) ++seq_;
    p.seq = seq_;
    return ready_ && broadcast(&p, sizeof(p));
  }
  bool broadcast(const void *data, size_t size) const {
    return ready_ && esp_now_send(BROADCAST, static_cast<const uint8_t *>(data), size) == ESP_OK;
  }

 private:
  static void receive(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
    if (length < int(sizeof(uint32_t))) return;
    Received m{};
    memcpy(&m.tag, data, sizeof(m.tag));
    if (m.tag == TAG && length == sizeof(Packet)) {
      memcpy(&m.packet, data, sizeof(m.packet));
      if (!valid(m.packet)) return;
    } else if (m.tag == RIDE_SAMPLE_TAG && length == sizeof(RideSample)) {
      memcpy(&m.sample, data, sizeof(m.sample));
      if (!m.sample.boot || !m.sample.ppr[0] || !m.sample.ppr[1] || !m.sample.goal[0] || !m.sample.goal[1] ||
          !validRideRate(m.sample.applied[0]) || !validRideRate(m.sample.applied[1])) return;
    } else if (m.tag == RIDE_RATE_TAG && length == sizeof(RideRate)) {
      memcpy(&m.rate, data, sizeof(m.rate));
      if (!m.rate.boot || !m.rate.run || !m.rate.revision ||
          !validRideRate(m.rate.value[0]) || !validRideRate(m.rate.value[1])) return;
    } else {
      return;
    }
    memcpy(m.mac, info->src_addr, sizeof(m.mac));
    xQueueSend(inbox_, &m, 0);
  }

  static inline QueueHandle_t inbox_ = nullptr;
  uint32_t boot_ = 0, seq_ = 0;
  bool ready_ = false;
};
}

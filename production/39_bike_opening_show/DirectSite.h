#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <atomic>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <freertos/queue.h>
#include <mbedtls/md.h>
#include <time.h>

class DirectSite {
 public:
  static IPAddress localAddress() { return IPAddress(192, 168, 4, 1); }

  void begin(uint8_t channel) {
    channel_ = channel;
    if (!readConfig()) return;
    if (!startBoot()) {
      state_ = State::Unavailable;
      return;
    }
    if (!config_.ssid[0]) {
      state_ = State::PhoneReady;
      return;
    }
    state_ = State::Offline;
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);

    // Do the channel check and join before the show can be armed. The AP and ESP-NOW share this radio.
    const int found = WiFi.scanNetworks(false, true, false, 180, channel_, config_.ssid, config_.bssid);
    WiFi.scanDelete();
    if (found <= 0) {
      state_ = State::RouterNotFound;
      return;
    }
    WiFi.begin(config_.ssid, config_.password, channel_, config_.bssid);
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 7000) delay(25);
    if (WiFi.status() != WL_CONNECTED || !checkNetwork()) {
      disconnect();
      return;
    }

    configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
    queue_ = xQueueCreate(1, sizeof(Snapshot));
    starts_ = xQueueCreate(1, sizeof(StartToken));
    rates_ = xQueueCreate(1, sizeof(StartToken));
    if (!queue_ || !starts_ || !rates_ || xTaskCreatePinnedToCore(upload, "sites", 12288, this, 1, nullptr, 0) != pdPASS) {
      if (queue_) vQueueDelete(queue_);
      if (starts_) vQueueDelete(starts_);
      if (rates_) vQueueDelete(rates_);
      queue_ = nullptr;
      starts_ = nullptr;
      rates_ = nullptr;
      state_ = State::Unavailable;
      disconnect();
    }
  }

  bool sampleDue(uint32_t now) {
    if (queue_ && now - lastCheck_ >= 250) {
      lastCheck_ = now;
      if (WiFi.status() == WL_CONNECTED && !checkNetwork()) disconnect();
    }
    if (!boot_ || now - lastSample_ < 1000) return false;
    lastSample_ = now;
    return true;
  }

  void publish(const String &json, uint32_t now) {
    constexpr char prefix[] = "tbt-sites-snapshot-v1\n";
    String message(prefix);
    if (json.length() == 0 || !message.reserve(json.length() + 96)) return;
    message += String("{\"boot\":") + String(boot_) + ",\"seq\":" + ++sequence_ + ",\"status\":" + json + "}";
    const char *body = message.c_str() + sizeof prefix - 1;
    const size_t size = message.length() - (sizeof prefix - 1);
    if (size == 0 || size >= sizeof(pending_.json)) return;
    uint8_t digest[32];
    const auto *sha = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!sha || mbedtls_md_hmac(sha, reinterpret_cast<const uint8_t *>(config_.key), 64,
                               reinterpret_cast<const uint8_t *>(message.c_str()), message.length(), digest)) return;
    constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof digest; ++i) {
      pending_.signature[2 * i] = hex[digest[i] >> 4];
      pending_.signature[2 * i + 1] = hex[digest[i] & 15];
    }
    previousSequence_ = pendingSequence_;
    previousSampledAt_ = pending_.sampledAt;
    pendingSequence_ = sequence_;
    pending_.sampledAt = now;
    memcpy(pending_.json, body, size + 1);
    if (queue_) xQueueOverwrite(queue_, &pending_);
  }

  void phoneResponse(WebServer &server, uint32_t now) const {
    server.sendHeader("Cache-Control", "no-store");
    if (!pending_.signature[0] || now - pending_.sampledAt > 2500) {
      const char *reason = boot_ ? "waiting-for-snapshot" :
          state_ == State::Unconfigured ? "unconfigured" : "unavailable";
      server.send(503, "text/plain", reason);
      return;
    }
    server.sendHeader("X-Snapshot-Signature", pending_.signature);
    server.send(200, "application/json", pending_.json);
  }

  bool verifyStart(const String &value, uint32_t run, uint32_t revision, uint32_t now,
                   uint8_t from = 0, uint8_t to = 0) const {
    const uint32_t sample = freshSequence(value, now);
    if (!sample) return false;
    String command = String(sample) + ':' + run + ':' + revision;
    if (to) command += String(':') + from + ':' + to;
    const char *prefix = to ? "tbt-sites-staff-advance-v1\n" : "tbt-sites-staff-start-v1\n";
    return verifyCommand(value, command, prefix);
  }

  bool verifyRideRate(const String &value, uint32_t run, uint32_t generation, uint32_t now,
                      uint32_t &revision, uint16_t (&rates)[2]) const {
    const uint32_t sample = freshSequence(value, now);
    if (!sample) return false;
    unsigned long signedSample, signedRun, signedGeneration, signedRevision;
    unsigned a, b;
    if (sscanf(value.c_str(), "%10lu:%10lu:%10lu:%10lu:%3u:%3u:", &signedSample, &signedRun,
               &signedGeneration, &signedRevision, &a, &b) != 6 ||
        signedSample != sample || signedRun != run || signedGeneration != generation ||
        !signedRevision || a < 50 || a > 200 || b < 50 || b > 200) return false;
    const String command = String(sample) + ':' + run + ':' + generation + ':' + signedRevision + ':' + a + ':' + b;
    if (!verifyCommand(value, command, "tbt-sites-ride-rate-v1\n")) return false;
    revision = signedRevision;
    rates[0] = a;
    rates[1] = b;
    return true;
  }

  String takeRideRate() const {
    StartToken value{};
    return rates_ && xQueueReceive(rates_, &value, 0) == pdTRUE ? String(value.text) : String();
  }

 private:
  uint32_t freshSequence(const String &value, uint32_t now) const {
    if (!boot_ || !pending_.signature[0] || value.length() >= sizeof(StartToken::text)) return 0;
    const char *first = value.c_str();
    const char *split = strchr(first, ':');
    if (!split || split == first || split - first > 10) return 0;
    uint64_t sample = 0;
    for (const char *digit = first; digit < split; ++digit) {
      if (*digit < '0' || *digit > '9') return 0;
      sample = sample * 10 + (*digit - '0');
    }
    if (!sample || !((sample == pendingSequence_ && now - pending_.sampledAt <= 2000) ||
                     (sample == previousSequence_ && now - previousSampledAt_ <= 2000))) return 0;
    return sample;
  }

  bool verifyCommand(const String &value, const String &command, const char *prefix) const {
    if (value.length() != command.length() + 65 || !value.startsWith(command) || value[command.length()] != ':') return false;
    const char *signature = value.c_str() + command.length() + 1;
    uint8_t digest[32];
    const String message = String(prefix) + String(boot_) + ':' + command;
    const auto *sha = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!sha || mbedtls_md_hmac(sha, reinterpret_cast<const uint8_t *>(config_.key), 64,
        reinterpret_cast<const uint8_t *>(message.c_str()), message.length(), digest)) return false;
    unsigned mismatch = 0;
    for (size_t i = 0; i < sizeof digest; ++i) {
      const int high = nibble(signature[2 * i]), low = nibble(signature[2 * i + 1]);
      if (high < 0 || low < 0) return false;
      mismatch |= digest[i] ^ (high * 16 + low);
    }
    return mismatch == 0;
  }

 public:
  bool takeStart(uint32_t run, uint32_t revision, uint32_t now,
                 uint8_t from = 0, uint8_t to = 0) const {
    StartToken value{};
    return starts_ && xQueueReceive(starts_, &value, 0) == pdTRUE &&
      verifyStart(String(value.text), run, revision, now, from, to);
  }

  bool command(const char *line, bool idle) const {
    if (strncmp(line, "site ", 5)) return false;
    const char *cmd = line + 5;
    if (!strcmp(cmd, "status")) {
      const bool online = queue_ && WiFi.status() == WL_CONNECTED;
      const int http = lastHttp_.load();
      const char *state = online ? (!clockReady() ? "waiting-for-clock" : "connected") : stateName(state_);
      Serial.printf("Sites: %s; HTTP=%d\n", state, online ? http : 0);
      return true;
    }
    if (!strcmp(cmd, "staff")) {
      if (!idle || !boot_ || !pendingSequence_ || millis() - pending_.sampledAt > 2000) {
        Serial.println("Sites: stop the show and wait for live sync before opening a staff session");
        return true;
      }
      const String signedFields = String(boot_) + ':' + pendingSequence_ + ':' + (esp_random() | 1U);
      const String message = String("tbt-sites-staff-pair-v1\n") + signedFields;
      uint8_t digest[32];
      const auto *sha = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
      if (!sha || mbedtls_md_hmac(sha, reinterpret_cast<const uint8_t *>(config_.key), 64,
            reinterpret_cast<const uint8_t *>(message.c_str()), message.length(), digest)) return true;
      Serial.print("Open on the staff device within 60s (one use): https://bicycle-illuminations-1128.feyhawthorn16.chatgpt.site/#staff-pair=");
      Serial.print(signedFields);
      Serial.print(':');
      for (uint8_t byte : digest) Serial.printf("%02x", byte);
      Serial.println();
      return true;
    }
    if (!idle) {
      Serial.println("Sites: refused outside standby");
      return true;
    }
    if (!strcmp(cmd, "clear")) {
      Preferences nvs;
      const bool opened = nvs.begin("tbt-sites", false);
      const bool cleared = opened && (!nvs.isKey("uplink-v1") || nvs.remove("uplink-v1"));
      if (opened) nvs.end();
      Serial.println(cleared ? "Sites: cleared; restart to apply" : "Sites: storage error");
      return true;
    }
    if (!strncmp(cmd, "configure ", 10) || !strncmp(cmd, "phone ", 6)) {
      Config candidate{};
      const bool phone = !strncmp(cmd, "phone ", 6);
      uint64_t seed = 0;
      if (phone ? !parsePhone(cmd + 6, candidate, seed) : !parseConfig(cmd + 10, candidate)) {
        Serial.println("Sites: invalid configuration");
        return true;
      }
      Preferences nvs;
      const bool opened = nvs.begin("tbt-sites", false);
      const bool seeded = opened && (!phone || nvs.getULong64("site-epoch", 0) >= seed ||
                         nvs.putULong64("site-epoch", seed) == sizeof seed);
      const bool saved = seeded && nvs.putBytes("uplink-v1", &candidate, sizeof candidate) == sizeof candidate;
      if (opened) nvs.end();
      Serial.println(saved ? "Sites: saved; restart to apply" : "Sites: storage error");
      return true;
    }
    Serial.println("Sites: site status; site staff; site clear");
    return true;
  }

 private:
  struct Config {
    uint8_t bssid[6];
    char ssid[33];
    char password[64];
    char key[65];
  };

  struct Snapshot {
    uint32_t sampledAt;
    char json[2400];
    char signature[65];
  };
  struct StartToken { char text[128]; };
  struct CommandTokens { StartToken start, rate; };

  enum class State { Unconfigured, PhoneReady, RouterNotFound, Offline, WrongChannel, SubnetConflict, Unavailable };
  static constexpr uint64_t MaxBoot = 9007199254740991ULL;

  static const char *stateName(State state) {
    switch (state) {
      case State::Unconfigured: return "unconfigured";
      case State::PhoneReady: return "phone-ready";
      case State::RouterNotFound: return "router-not-found-on-channel-6";
      case State::Offline: return "offline";
      case State::WrongChannel: return "wrong-channel";
      case State::SubnetConflict: return "subnet-conflict";
      case State::Unavailable: return "uploader-unavailable";
    }
    return "unavailable";
  }

  bool readConfig() {
    Preferences nvs;
    if (!nvs.begin("tbt-sites", true)) return false;
    const bool loaded = nvs.getBytesLength("uplink-v1") == sizeof config_ &&
                        nvs.getBytes("uplink-v1", &config_, sizeof config_) == sizeof config_;
    nvs.end();
    return loaded && config_.ssid[32] == '\0' && config_.password[63] == '\0' &&
           config_.key[64] == '\0' && isKey(config_.key);
  }

  bool startBoot() {
    Preferences nvs;
    if (!nvs.begin("tbt-sites", false)) return false;
    const uint64_t last = nvs.getULong64("site-epoch", 0);
    const bool saved = last < MaxBoot && nvs.putULong64("site-epoch", last + 1) == sizeof last;
    nvs.end();
    if (saved) boot_ = last + 1;
    return saved;
  }

  static int nibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
  }

  static bool isKey(const char *key) {
    if (strlen(key) != 64) return false;
    for (size_t i = 0; i < 64; ++i) if (nibble(key[i]) < 0) return false;
    return true;
  }

  static bool parsePhone(const char *fields, Config &config, uint64_t &seed) {
    const char *end = strchr(fields, ' ');
    if (!end || end - fields < 1 || end - fields > 15 || !isKey(end + 1)) return false;
    for (const char *digit = fields; digit < end; ++digit) {
      if (*digit < '0' || *digit > '9') return false;
      seed = seed * 10 + (*digit - '0');
    }
    if (!seed) return false;
    memcpy(config.key, end + 1, 64);
    return true;
  }

  static bool decode(const char *&field, uint8_t *dst, size_t min, size_t max, bool text) {
    const char *end = strchr(field, ' ');
    if (!end) return false;
    const size_t length = end - field;
    if (length % 2 || length / 2 < min || length / 2 > max) return false;
    for (size_t i = 0; i < length / 2; ++i) {
      const int high = nibble(field[2 * i]), low = nibble(field[2 * i + 1]);
      if (high < 0 || low < 0 || (text && high == 0 && low == 0)) return false;
      dst[i] = (high << 4) | low;
    }
    field = end + 1;
    return true;
  }

  static bool parseConfig(const char *fields, Config &config) {
    if (!decode(fields, config.bssid, 6, 6, false) || config.bssid[0] & 1 ||
        !decode(fields, reinterpret_cast<uint8_t *>(config.ssid), 1, 32, true) ||
        !decode(fields, reinterpret_cast<uint8_t *>(config.password), 8, 63, true) ||
        !isKey(fields)) return false;
    memcpy(config.key, fields, 64);
    return true;
  }

  bool checkNetwork() {
    uint8_t primary = 0;
    wifi_second_chan_t secondary;
    if (esp_wifi_get_channel(&primary, &secondary) != ESP_OK || primary != channel_) {
      state_ = State::WrongChannel;
      return false;
    }
    // Use the less specific netmask so a router on a wider subnet is rejected as well.
    const uint32_t sharedMask = uint32_t(WiFi.subnetMask()) & uint32_t(IPAddress(255, 255, 255, 0));
    if ((uint32_t(WiFi.localIP()) & sharedMask) == (uint32_t(localAddress()) & sharedMask)) {
      state_ = State::SubnetConflict;
      return false;
    }
    return true;
  }

  void disconnect() {
    allowed_ = false;
    esp_wifi_disconnect();
    esp_wifi_set_channel(channel_, WIFI_SECOND_CHAN_NONE);
  }

  static bool clockReady() { return time(nullptr) >= 1704067200; }

  static void upload(void *context) {
    static_cast<DirectSite *>(context)->uploadLoop();
  }

  static esp_err_t responseEvent(esp_http_client_event_t *event) {
    auto *tokens = static_cast<CommandTokens *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_HEADER) {
      StartToken *token = !strcasecmp(event->header_key, "X-TBT-Staff-Start") ? &tokens->start :
          !strcasecmp(event->header_key, "X-TBT-Ride-Rate") ? &tokens->rate : nullptr;
      if (token) {
        const size_t length = strlen(event->header_value);
        if (length < sizeof token->text) memcpy(token->text, event->header_value, length + 1);
      }
    }
    return ESP_OK;
  }

  void uploadLoop() {
    esp_http_client_config_t http{};
    http.url = "https://bicycle-illuminations-1128.feyhawthorn16.chatgpt.site/sync";
    http.method = HTTP_METHOD_POST;
    http.crt_bundle_attach = esp_crt_bundle_attach;
    http.disable_auto_redirect = true;
    http.timeout_ms = 2000;
    CommandTokens tokens{};
    http.user_data = &tokens;
    http.event_handler = responseEvent;
    const esp_http_client_handle_t client = esp_http_client_init(&http);
    if (!client || esp_http_client_set_header(client, "Content-Type", "application/json") != ESP_OK) {
      if (client) esp_http_client_cleanup(client);
      lastHttp_ = -1;
      vTaskDelete(nullptr);
      return;
    }

    Snapshot snapshot;
    while (true) {
      xQueueReceive(queue_, &snapshot, portMAX_DELAY);
      if (!allowed_.load() || WiFi.status() != WL_CONNECTED || !clockReady() ||
          millis() - snapshot.sampledAt >= 2500) continue;
      const esp_err_t header = esp_http_client_set_header(client, "X-Snapshot-Signature", snapshot.signature);
      const esp_err_t body = header == ESP_OK ? esp_http_client_set_post_field(client, snapshot.json, strlen(snapshot.json)) : header;
      tokens.start.text[0] = tokens.rate.text[0] = '\0';
      const esp_err_t sent = body == ESP_OK ? esp_http_client_perform(client) : body;
      lastHttp_ = sent == ESP_OK ? esp_http_client_get_status_code(client) : -1;
      if (lastHttp_.load() == 204 && tokens.start.text[0]) xQueueOverwrite(starts_, &tokens.start);
      if (lastHttp_.load() == 204 && tokens.rate.text[0]) xQueueOverwrite(rates_, &tokens.rate);
      if (lastHttp_.load() != 204) {
        esp_http_client_close(client);
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    }
  }

  Config config_{};
  Snapshot pending_{};
  QueueHandle_t queue_ = nullptr;
  QueueHandle_t starts_ = nullptr;
  QueueHandle_t rates_ = nullptr;
  std::atomic<int> lastHttp_{0};
  std::atomic<bool> allowed_{true};
  State state_ = State::Unconfigured;
  uint8_t channel_ = 0;
  uint64_t boot_ = 0;
  uint32_t sequence_ = 0;
  uint32_t pendingSequence_ = 0, previousSequence_ = 0, previousSampledAt_ = 0;
  uint32_t lastSample_ = 0, lastCheck_ = 0;
};

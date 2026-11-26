/*
 * ESP32の翼基板Aへ書き込む。既設のFC-33 2個の回転速度を27番へ無線で送る。
 * 配線は翼A Rev. E。自転車AのOUT → GPIO34、自転車BのOUT → GPIO35。
 * XIAO_ESP32C3を選ぶ予備機ではA → D4(GPIO6)、B → D5(GPIO7)。USB CDC On BootはEnabled。
 * センサーVCCは3V3、GNDは翼基板のGND。27番とはESP-NOW（チャンネル6）で通信。
 * シリアルモニタ115200 bps・改行あり。helpで操作一覧。
 * status: A/B入力の現在レベル、受理パルス累計、A/BのRPM、送信キューの結果。
 * count zero のあと片側を回し count を送ると、基準からのA/Bの増分を確認できる。送信値は変えない。
 * watch: 自動表示の周期確認。watch 1000 で1秒ごと、watch off で停止。
 * config: 回転換算の現在値・範囲。config ppr_a は1項目、config export は復元用set行。
 * 立ち下がりのみを数える。初期値はスリット円板1回転につきA 20、B 12パルス。
 * 次のパルスが遅れれば表示は下がり、最後のパルスから1.2秒で0 rpmになる。
 * 回転計測・送信の切り分け用。本番では翼の演出と送信を43番で行う。
 */
#include "src/RiderSpeedRadio.h"
#include "src/OpeningShow/LocalControls.h"
#include "src/OpeningShow/Settings.h"
#include <esp_timer.h>

#if defined(ARDUINO_XIAO_ESP32C3)
constexpr uint8_t SENSOR_A = D4;
constexpr uint8_t SENSOR_B = D5;
#else
constexpr uint8_t SENSOR_A = 34;
constexpr uint8_t SENSOR_B = 35;
#endif
constexpr uint16_t PULSES_PER_REV_A = 6;
constexpr uint16_t PULSES_PER_REV_B = 6;
constexpr uint32_t MIN_GAP_US = 1500;
constexpr uint32_t STOP_US = 1200000;
constexpr uint32_t SEND_MS = 100;
constexpr uint8_t BROADCAST_MAC[] = {255, 255, 255, 255, 255, 255};
enum SenderSetting { PprA, PprB };
OpeningShow::Setting options[] = {{"ppr_a", PULSES_PER_REV_A, 1, 1000}, {"ppr_b", PULSES_PER_REV_B, 1, 1000}};
OpeningShow::Settings config("tbt-wing-fx", options);
OpeningShow::Console console;
OpeningShow::ConsoleWatch watch(1000);

struct PulseTiming {
  int64_t lastEdgeUs;
  uint32_t periodUs;
  uint32_t total;
};

portMUX_TYPE pulseLock = portMUX_INITIALIZER_UNLOCKED;
PulseTiming pulseA{}, pulseB{};
bool radioReady = false;
bool queued = false;
uint32_t lastSend = 0;

void ARDUINO_ISR_ATTR onPulse(void *arg) {
  auto &timing = *static_cast<PulseTiming *>(arg);
  const int64_t now = esp_timer_get_time();
  portENTER_CRITICAL_ISR(&pulseLock);
  const int64_t gap = now - timing.lastEdgeUs;
  if (!timing.lastEdgeUs || gap >= MIN_GAP_US) {
    if (!timing.lastEdgeUs || gap >= STOP_US) {
      timing.periodUs = 0;
    } else {
      const uint32_t period = static_cast<uint32_t>(gap);
      timing.periodUs = timing.periodUs ? (3 * timing.periodUs + period) / 4 : period;
    }
    timing.lastEdgeUs = now;
    ++timing.total;
  }
  portEXIT_CRITICAL_ISR(&pulseLock);
}

uint16_t rpmTenths(const PulseTiming &timing, uint16_t pulsesPerRev, int64_t nowUs) {
  const int64_t elapsedUs = nowUs - timing.lastEdgeUs;
  if (!timing.periodUs || elapsedUs >= STOP_US) return 0;
  // Once an edge is overdue, the last measured period would overstate the speed.
  const uint32_t periodUs = max(timing.periodUs, static_cast<uint32_t>(elapsedUs));
  const uint32_t value = 600000000UL / (periodUs * pulsesPerRev);
  return min(value, uint32_t(UINT16_MAX));
}

void help() {
  Serial.printf("26 RPM sender / diagnosis | 115200 bps, newline | A=GPIO%u, B=GPIO%u, falling edges\n",
      unsigned(SENSOR_A), unsigned(SENSOR_B));
  Serial.println("status: levels, accepted pulses, RPM and TX queue | count zero then count: check one sensor at a time");
  Serial.println("TX queued: check the receiver with status on 27. This sketch does not drive wing LEDs.");
  OpeningShow::ConsoleWatch::help();
  OpeningShow::Settings::help();
}

void setup() {
  Serial.begin(115200);
  config.begin();
  pinMode(SENSOR_A, INPUT);
  pinMode(SENSOR_B, INPUT);
  attachInterruptArg(SENSOR_A, onPulse, &pulseA, FALLING);
  attachInterruptArg(SENSOR_B, onPulse, &pulseB, FALLING);

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, BROADCAST_MAC, sizeof(BROADCAST_MAC));
  peer.channel = RiderSpeedRadio::CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  radioReady = RiderSpeedRadio::begin() && esp_now_add_peer(&peer) == ESP_OK;
  Serial.printf("RPM sender (channel %u) %s\n", RiderSpeedRadio::CHANNEL,
                radioReady ? "ready" : "radio init failed");
  help();
}

void loop() {
  const uint32_t now = millis();
  const char *cmd = console.read();
  const bool status = cmd && !strcmp(cmd, "status");
  const bool zero = cmd && !strcmp(cmd, "count zero");
  const bool count = zero || (cmd && !strcmp(cmd, "count"));
  if (cmd && !status && !count && !watch.command(cmd, now) && !config.command(cmd, true)) help();
  const bool send = now - lastSend >= SEND_MS;
  const bool report = status || watch.due(now);
  if (!send && !report && !count) {
    delay(1);
    return;
  }
  portENTER_CRITICAL(&pulseLock);
  const PulseTiming a = pulseA;
  const PulseTiming b = pulseB;
  portEXIT_CRITICAL(&pulseLock);
  if (count) {
    static uint32_t baseA = 0, baseB = 0;
    if (zero) { baseA = a.total; baseB = b.total; }
    Serial.printf("since count zero A=%lu B=%lu\n", (unsigned long)(a.total - baseA), (unsigned long)(b.total - baseB));
  }
  const int64_t nowUs = esp_timer_get_time();
  const RiderSpeedRadio::Packet packet{RiderSpeedRadio::PACKET_TAG,
      rpmTenths(a, config[PprA], nowUs), rpmTenths(b, config[PprB], nowUs)};
  if (send) {
    lastSend = now;
    queued = radioReady && esp_now_send(BROADCAST_MAC,
        reinterpret_cast<const uint8_t *>(&packet), sizeof(packet)) == ESP_OK;
  }
  if (report) {
    watch.printed(now);
    Serial.printf("gpio A=%u B=%u pulses A=%lu B=%lu RPM A=%u.%u B=%u.%u tx=%s\n",
        digitalRead(SENSOR_A), digitalRead(SENSOR_B), (unsigned long)a.total, (unsigned long)b.total,
        packet.rpmTenthsA / 10U, packet.rpmTenthsA % 10U,
        packet.rpmTenthsB / 10U, packet.rpmTenthsB % 10U,
        queued ? "queued" : !radioReady ? "radio error" : now < SEND_MS ? "waiting" : "error");
  }
}

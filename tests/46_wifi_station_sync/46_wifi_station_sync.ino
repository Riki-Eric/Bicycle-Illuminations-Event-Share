// ESP32 Dev Module / XIAO ESP32C3（USB CDC On Bootを有効にする）。
// 光らせず、各ESPの2.4 GHzテザリング接続とESP間の往復通信をUSBシリアルで確認する。
// 115200 bps、改行あり。ssid <Wi-Fi名> → pass <パスワード>。入力値は出力・永続化しない。
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_random.h>

constexpr uint16_t PORT = 28346;
constexpr uint32_t MAGIC = 0x34365442;
struct Message { uint32_t magic, sender, target, seq; };
struct Peer { uint32_t id, received, returned; };
Peer peers[24]{};
WiFiUDP udp;
char ssid[33]{}, input[72]{};
uint8_t used = 0;
bool discard = false, online = false;
const uint32_t device = esp_random() | 1;
uint32_t lastPoll = 0, sequence = 0;

void help() {
  Serial.println("46 station sync | 115200 newline | ssid <Wi-Fi name>, pass <password>, status");
}

void command() {
  if (strncmp(input, "ssid ", 5) == 0 && strlen(input + 5) <= 32 && input[5]) {
    strcpy(ssid, input + 5);
    Serial.println("SSID accepted; send pass next.");
  } else if (strncmp(input, "pass ", 5) == 0 && strlen(input + 5) >= 8 &&
             strlen(input + 5) <= 63 && ssid[0]) {
    udp.stop();
    online = false;
    for (Peer &peer : peers) peer = {};
    WiFi.disconnect();
    WiFi.begin(ssid, input + 5);
    Serial.println("Joining Wi-Fi...");
  } else if (strcmp(input, "status") == 0) {
    Serial.printf("id=%08lx wifi=%s ch=%d ip=%s\n", (unsigned long)device,
      online ? "online" : "waiting", WiFi.channel(), WiFi.localIP().toString().c_str());
    const uint32_t now = millis();
    for (const Peer &peer : peers) if (peer.id) {
      Serial.printf("peer=%08lx rx=%s roundtrip=%s\n", (unsigned long)peer.id,
        peer.received && now - peer.received < 4000 ? "OK" : "--",
        peer.returned && now - peer.returned < 4000 ? "OK" : "--");
    }
  } else help();
}

void sendMessage(IPAddress host, uint16_t port, const Message &message) {
  if (!udp.beginPacket(host, port)) return;
  udp.write(reinterpret_cast<const uint8_t*>(&message), sizeof(message));
  udp.endPacket();
}

void receive(uint32_t now) {
  const int size = udp.parsePacket();
  if (!size) return;
  Message message{};
  if (size != sizeof(message)) { udp.clear(); return; }
  if (udp.read(reinterpret_cast<uint8_t*>(&message), sizeof(message)) != sizeof(message) ||
      message.magic != MAGIC || !message.sender || message.sender == device ||
      (message.target && (message.target != device || !message.seq || sequence - message.seq > 3))) return;
  for (Peer &peer : peers) {
    if (peer.id && peer.id != message.sender) continue;
    if (!peer.id) Serial.printf("Discovered peer=%08lx\n", (unsigned long)message.sender);
    peer.id = message.sender;
    if (message.target) peer.returned = now;
    else peer.received = now;
    break;
  }
  if (!message.target) sendMessage(udp.remoteIP(), udp.remotePort(), {MAGIC, device, message.sender, message.seq});
}

void setup() {
  Serial.begin(115200);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  help();
}

void loop() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (!discard && used) { input[used] = 0; command(); }
      memset(input, 0, sizeof(input)); used = 0; discard = false;
    } else if (used < sizeof(input) - 1 && !discard) input[used++] = c;
    else discard = true;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (online) { udp.stop(); online = false; Serial.println("Wi-Fi disconnected"); }
    delay(10);
    return;
  }
  if (!online) {
    online = udp.begin(PORT);
    if (!online) { delay(250); return; }
    Serial.printf("Connected: IP %s, channel %d\n", WiFi.localIP().toString().c_str(), WiFi.channel());
  }
  const uint32_t now = millis();
  receive(now);
  if (now - lastPoll >= 1000) {
    lastPoll = now;
    const IPAddress local = WiFi.localIP(), mask = WiFi.subnetMask();
    IPAddress broadcast;
    for (uint8_t i = 0; i < 4; ++i) broadcast[i] = local[i] | uint8_t(~mask[i]);
    if (++sequence == 0) ++sequence;
    sendMessage(broadcast, PORT, {MAGIC, device, 0, sequence});
  }
}

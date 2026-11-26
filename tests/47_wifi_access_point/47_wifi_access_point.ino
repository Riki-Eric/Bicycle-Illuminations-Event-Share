// ESP32 Dev Module / XIAO ESP32C3（USB CDC On Bootを有効にする）。
// スマホがESPのWi-Fiに接続し、ESPからHTTPを受け取れるかだけを確認する。
// 115200 bps。SSIDと起動ごとに生成するパスワードをUSBシリアルに表示する。
#include <WiFi.h>
#include <WebServer.h>
#include <esp_random.h>

constexpr char SSID[] = "Illumination-WiFi-Test";
WebServer server(80);
bool running = false;

void setup() {
  Serial.begin(115200);
  delay(900);
  WiFi.persistent(false);
  char password[17];
  snprintf(password, sizeof(password), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
  running = WiFi.mode(WIFI_AP) && WiFi.softAP(SSID, password, 6, false, 2);
  if (!running) { Serial.println("Wi-Fi AP start failed."); return; }
  Serial.printf("SSID: %s\nPassword: %s\nOpen http://%s/\n", SSID, password, WiFi.softAPIP().toString().c_str());
  server.on("/", HTTP_GET, [] {
    server.send(200, "text/plain; charset=utf-8", "47 OK - phone received a response from the ESP.");
    Serial.println("HTTP OK: received a request and returned 200.");
  });
  server.begin();
}

void loop() {
  if (running) server.handleClient();
  if (running && Serial.available() && Serial.read() == 's')
    Serial.printf("Connected phones: %u\n", WiFi.softAPgetStationNum());
  delay(4);
}

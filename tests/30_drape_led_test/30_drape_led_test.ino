// 10 m・100灯のドレープ1本をESP32-DevKitC V4（WROOM-32E）で試験する。
// 裸のDevKit: GPIO18 → 5 V駆動のSN74AHCT125N → 330 Ω → LEDのDIN。
// LEDは外部DC5 Vで給電し、ESP32・変換IC・LEDのGNDを共通にする。
// 手はんだ版: GPIO23で出力許可。J3-1はDIN、J7-1は+5 V、J7-2はGND。
// シリアル115200 bps。r/g/b/wで全灯を高輝度で5秒間点灯、sで低輝度の1灯を順送り、0で消灯、?で操作一覧。
// 起動時と試験終了時に全消灯データを送る。発光はLED現物、操作結果はシリアルで確認。

#include <Arduino.h>
#include <esp32-hal-rmt.h>

namespace {
constexpr int DATA_PIN = 18;
constexpr int DATA_ENABLE_PIN = 23;
constexpr uint16_t PIXELS = 100;
constexpr uint16_t ALL_PIXELS = PIXELS;
constexpr uint8_t SOLID_LEVEL = 192;
constexpr uint8_t SCAN_LEVEL = 12;
constexpr uint32_t SOLID_MS = 5000;
constexpr uint32_t SCAN_STEP_MS = 70;

constexpr uint32_t RED = static_cast<uint32_t>(SOLID_LEVEL) << 8;
constexpr uint32_t GREEN = static_cast<uint32_t>(SOLID_LEVEL) << 16;
constexpr uint32_t BLUE = SOLID_LEVEL;
constexpr uint32_t WHITE = RED | GREEN | BLUE;

enum class Mode : uint8_t { Off, Solid, Scan };
Mode mode = Mode::Off;
bool ready = false;
uint16_t activePixel = 0;
uint32_t changedAt = 0;
rmt_data_t frame[PIXELS * 24 + 1]{};

bool sendFrame(uint32_t grb, uint16_t litPixel = ALL_PIXELS) {
  size_t cursor = 0;
  const bool lightAll = litPixel == ALL_PIXELS;
  for (uint16_t pixel = 0; pixel < PIXELS; ++pixel) {
    const uint32_t color = (lightAll || pixel == litPixel) ? grb : 0;
    for (uint32_t bit = 0x800000; bit != 0; bit >>= 1) {
      const bool one = (color & bit) != 0;
      rmt_data_t &symbol = frame[cursor++];
      // WS2812B-4020の時間範囲。20 MHzの1 tickは50 ns。
      symbol.level0 = 1;
      symbol.duration0 = one ? 13 : 7;
      symbol.level1 = 0;
      symbol.duration1 = one ? 12 : 18;
    }
  }
  rmt_data_t &reset = frame[cursor++];
  reset.level0 = 0;
  reset.duration0 = 6000;
  reset.level1 = 0;
  reset.duration1 = 1;
  if (rmtWrite(DATA_PIN, frame, cursor, RMT_WAIT_FOR_EVER)) return true;

  digitalWrite(DATA_ENABLE_PIN, LOW);
  ready = false;
  mode = Mode::Off;
  Serial.println("ERR data output failed; turn off LED power");
  return false;
}

void showHelp() {
  Serial.println("r/g/b/w: all LEDs bright for 5 s; s: dim scan; 0: off; ?: help");
}

void startTest(Mode test, uint32_t grb) {
  activePixel = 0;
  if (!sendFrame(grb, test == Mode::Scan ? activePixel : ALL_PIXELS)) return;
  mode = test;
  changedAt = millis();
  Serial.println(test == Mode::Scan ? "TX scan started" : "TX all LEDs");
}

void turnOff() {
  mode = Mode::Off;
  if (sendFrame(0)) Serial.println("TX off");
}

void handleCommand(char command) {
  switch (command) {
    case 'r': startTest(Mode::Solid, RED); break;
    case 'g': startTest(Mode::Solid, GREEN); break;
    case 'b': startTest(Mode::Solid, BLUE); break;
    case 'w': startTest(Mode::Solid, WHITE); break;
    case 's': startTest(Mode::Scan, SCAN_LEVEL); break;
    case '0': turnOff(); break;
    case '\r':
    case '\n':
    case ' ': break;
    default: showHelp(); break;
  }
}
}  // namespace

void setup() {
  pinMode(DATA_ENABLE_PIN, OUTPUT);
  digitalWrite(DATA_ENABLE_PIN, LOW);
  Serial.begin(115200);
  if (!rmtInit(DATA_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 20000000) ||
      !rmtSetEOT(DATA_PIN, LOW)) {
    Serial.println("ERR GPIO18 RMT initialization failed");
    return;
  }
  digitalWrite(DATA_ENABLE_PIN, HIGH);
  ready = sendFrame(0);
  if (ready) showHelp();
}

void loop() {
  if (!ready) return;
  if (Serial.available() > 0) handleCommand(static_cast<char>(Serial.read()));

  const uint32_t now = millis();
  if (mode == Mode::Solid && now - changedAt >= SOLID_MS) {
    turnOff();
  } else if (mode == Mode::Scan && now - changedAt >= SCAN_STEP_MS) {
    ++activePixel;
    if (activePixel == PIXELS) {
      turnOff();
    } else if (sendFrame(SCAN_LEVEL, activePixel)) {
      changedAt = now;
    }
  }
}

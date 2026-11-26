// 翼Aのユニバーサル基板用。5 V・16 MHzのUNO R3／classic Nano。
// WING_USE_A0_COMPARATOR は 0: D8（Q1経由）、1: A0（内蔵コンパレータ）。
// A0版はXIAOのD10（DevKitCではGPIO18）から10 kΩを直列、A0を47 kΩでGNDへ。
// 配線と給電順は翼AのREADMEを参照。
// D8版のD9は内部の送信設定用。A0版はTimer1を受信に使う。
// D6 → 470 Ω → 翼のDIN。翼12 Vはヒューズ付きの外部配線。両基板と翼の信号GNDを共通にする。
// ESP32の43番をArduino bridge設定で使用。内蔵LEDは受信でき、消灯指令で待機できたときに点灯。
// 43番のtestで左右低輝度点灯、stopで消灯。受信途絶後は次の消灯指令まで再描画しない。
// Arduino側のUSBシリアルは115200 bps・改行あり。helpで入力一覧、statusでD8/A0・受信許可・受信数を確認。
// 43番のmark a root等で現物を見る。44番のUSBから点灯は指示しない。D8/A0の信号速度は38400 bps。
#ifndef WING_USE_A0_COMPARATOR
#define WING_USE_A0_COMPARATOR 0
#endif
static_assert(WING_USE_A0_COMPARATOR == 0 || WING_USE_A0_COMPARATOR == 1,
              "Choose 0 (D8) or 1 (A0)");
#if WING_USE_A0_COMPARATOR
#include "src/A0ComparatorSerial.h"
#define WING_BRIDGE_INPUT "A0"
#else
#include <SoftwareSerial.h>
#define WING_BRIDGE_INPUT "D8"
#endif
#include "src/Adafruit_NeoPixel/Adafruit_NeoPixel.h"
#include "src/WingBridgeWire.h"

using namespace WingBridge;
constexpr uint8_t DataPin = 6;
#if WING_USE_A0_COMPARATOR
A0ComparatorSerial link;
#else
constexpr uint8_t RxPin = 8, UnusedTxPin = 9;
SoftwareSerial link(RxPin, UnusedTxPin);
#endif
Adafruit_NeoPixel strip(Pixels, DataPin, NEO_RGB + NEO_KHZ800);

uint8_t colors[ColorBytes];
uint16_t position = 0, crc = 0xffff, receivedCrc = 0;
uint8_t command = 0;
uint32_t lastByte = 0, lastValid = 0;
bool enabled = false, lit = false;
uint16_t blankCount = 0, paintCount = 0, badCrcCount = 0, overflowCount = 0;
char serialReply[115];
uint8_t replyAt = 0;

void queueHelp() {
  strcpy_P(serialReply, PSTR("44 / " WING_BRIDGE_INPUT " | USB 115200 newline | help | status: RX counts | use 43 mark/test/stop for LEDs\r\n"));
}

void serviceUsbConsole() {
  // Send one queued byte at a time without waiting for the USB UART TX buffer.
  if (serialReply[replyAt]) {
    if (Serial.availableForWrite()) Serial.write(serialReply[replyAt++]);
    return;
  }
  if (!Serial.available()) return;
  static char input[8];
  static uint8_t used = 0;
  static bool invalid = false;
  const char ch = Serial.read();
  if (ch == '\r' || ch == '\n') {
    input[used] = '\0';
    replyAt = 0;
    if (!invalid && !strcmp(input, "status")) {
      snprintf_P(serialReply, sizeof serialReply,
          PSTR("44 / " WING_BRIDGE_INPUT " rx=%s blank=%u paint=%u crc=%u overflow=%u (counters wrap)\r\n"),
          enabled ? "ready" : "waiting for blank", blankCount, paintCount, badCrcCount, overflowCount);
    } else if (used || invalid) {
      queueHelp();
    } else serialReply[0] = '\0';
    used = 0;
    invalid = false;
  } else if (used + 1 < sizeof input && !invalid && ch >= ' ' && ch <= '~') {
    input[used++] = ch;
  } else invalid = true;
}

void blankStrip() {
  strip.clear();
  strip.show();
  lit = false;
}

void handleByte(uint8_t value, uint32_t now) {
  if (now - lastByte >= GapMs) position = 0;
  lastByte = now;

  if (position == 0) {
    if (value == Sync1) position = 1;
    return;
  }
  if (position == 1) {
    position = value == Sync2 ? 2 : value == Sync1 ? 1 : 0;
    return;
  }
  if (position == 2) {
    position = 0;
    if (value == Blank || value == Paint) {
      command = value;
      crc = crcByte(0xffff, value);
      position = 3;
    }
    return;
  }
  const uint16_t dataEnd = 3 + (command == Paint ? ColorBytes : 0);
  if (position < dataEnd) {
    colors[position++ - 3] = value;
    crc = crcByte(crc, value);
    return;
  }
  if (position++ == dataEnd) {
    receivedCrc = uint16_t(value) << 8;
    return;
  }
  position = 0;
  if ((receivedCrc | value) != crc) { ++badCrcCount; return; }
  if (command == Blank) {
    ++blankCount;
    if (lit) blankStrip();
    enabled = true;
    digitalWrite(LED_BUILTIN, HIGH);
  } else if (enabled) {
    ++paintCount;
    memcpy(strip.getPixels(), colors, ColorBytes);
    strip.show();
    lit = true;
  } else {
    return;
  }
  lastValid = now;
}

void setup() {
  Serial.begin(115200);
  queueHelp();
  pinMode(LED_BUILTIN, OUTPUT);
  strip.begin();
  if (strip.numPixels() != Pixels) return;
  blankStrip();
#if WING_USE_A0_COMPARATOR
  link.begin();
#else
  link.begin(Baud);
#endif
}

void loop() {
  const uint32_t now = millis();
  if (enabled && now - lastValid >= TimeoutMs) {
    enabled = false;
    position = 0;
    digitalWrite(LED_BUILTIN, LOW);
    if (lit) blankStrip();
  }
  if (link.overflow()) { ++overflowCount; position = 0; }
  if (link.available()) handleByte(uint8_t(link.read()), now);
  else if (!position || now - lastByte >= GapMs) serviceUsbConsole();
}

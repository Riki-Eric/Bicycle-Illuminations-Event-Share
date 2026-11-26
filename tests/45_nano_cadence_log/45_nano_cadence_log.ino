// 翼の予備基板で行う回転の単体計測。classic Nano / ATmega328P。LEDと無線は使わない。
// 回転センサーA/BのOUTを仮ジャンパでD2/D3へ。センサー電源3.3 Vと共通GNDは予備機の図面に従う。
// 115200 bps、改行あり。help：説明、start：60秒計測、stop：途中終了。
// STARTからENDまでを保存する。shaft_rpmは円板の回転速度。ペダル回転との比は別途実測する。
#include <Arduino.h>
#include <util/atomic.h>
#include <string.h>

constexpr uint8_t SENSOR_A = 2, SENSOR_B = 3;
constexpr uint8_t SLOTS = 6;
constexpr uint32_t MIN_EDGE_US = 3500, DURATION_MS = 60000, SAMPLE_MS = 1000;
volatile uint32_t pulses[2]{}, lastEdge[2]{};
volatile bool seen[2]{};
uint32_t started, sampled, previous[2]{};
bool recording = false, discardLine = false;
char line[12];
uint8_t used = 0;

void pulse(uint8_t side) {
  const uint32_t now = micros();
  if (!seen[side] || now - lastEdge[side] >= MIN_EDGE_US) {
    lastEdge[side] = now;
    seen[side] = true;
    ++pulses[side];
  }
}
void pulseA() { pulse(0); }
void pulseB() { pulse(1); }

void printHelp() {
  Serial.println(F("45 Nano cadence | 115200 newline | start=60s, stop, help"));
  Serial.println(F("D2=A D3=B, one falling edge per slot, 6 slots per measured shaft revolution"));
}

void printSample(uint32_t now) {
  const uint32_t elapsed = now - sampled;
  if (!elapsed) return;
  uint32_t current[2];
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    current[0] = pulses[0]; current[1] = pulses[1];
  }
  Serial.print(now - started);
  for (uint8_t side = 0; side < 2; ++side) {
    const uint32_t delta = current[side] - previous[side];
    Serial.print(','); Serial.print(delta);
    Serial.print(','); Serial.print(current[side]);
    Serial.print(','); Serial.print(delta * 60000.0 / (SLOTS * float(elapsed)), 2);
    previous[side] = current[side];
  }
  Serial.println();
  sampled = now;
}

void command(uint32_t now) {
  if (!strcmp(line, "help")) printHelp();
  else if (!strcmp(line, "start") && !recording) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      pulses[0] = pulses[1] = 0;
      seen[0] = seen[1] = false;
    }
    previous[0] = previous[1] = 0;
    started = sampled = now;
    recording = true;
    Serial.println(F("START,slots=6,interval_ms=1000,duration_ms=60000"));
    Serial.println(F("ms,a_interval,a_total,a_shaft_rpm,b_interval,b_total,b_shaft_rpm"));
  } else if (!strcmp(line, "stop") && recording) {
    printSample(now);
    recording = false;
    Serial.println(F("END,manual"));
  } else Serial.println(F("Use help; stop before starting another measurement."));
}

void setup() {
  pinMode(SENSOR_A, INPUT);
  pinMode(SENSOR_B, INPUT);
  attachInterrupt(digitalPinToInterrupt(SENSOR_A), pulseA, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_B), pulseB, FALLING);
  Serial.begin(115200);
  printHelp();
}

void loop() {
  while (Serial.available()) {
    const char input = Serial.read();
    if (input == '\r') continue;
    if (input == '\n') {
      if (!discardLine && used) { line[used] = 0; command(millis()); }
      used = 0;
      discardLine = false;
    } else if (used < sizeof(line) - 1 && !discardLine) line[used++] = input;
    else discardLine = true;
  }
  const uint32_t now = millis();
  if (!recording) return;
  if (now - sampled >= SAMPLE_MS || now - started >= DURATION_MS) printSample(now);
  if (now - started >= DURATION_MS) {
    recording = false;
    Serial.println(F("END,complete"));
  }
}

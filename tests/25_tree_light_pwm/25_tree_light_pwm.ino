// DDL112BGS 1本の金・白を200 HzのPWMで独立に調光する単体試験。
// ESP32-DevKitC-32EをUSB給電し、GPIO25（金）とGPIO26（白）を
// 5 VのSN74AHCT125経由で各Nch MOSFETのゲートへ。GNDは24 V電源のマイナスと共有する。
// ライトの1番は+24 V、2番と3番は各MOSFETのドレイン。配線と初回通電はdocs/tree-light-inspection.md参照。
// シリアルモニタは115200 bps、改行あり。両色消灯で起動し、g 25 / s 75 / set 25 75 などを送る。
// 数値は点灯時間の割合（0〜100%）。offで両色消灯、statusで設定を表示する。

#include <Arduino.h>
#include <string.h>

namespace {
constexpr uint8_t GOLD_PIN = 25;
constexpr uint8_t SILVER_PIN = 26;
constexpr uint32_t PWM_HZ = 200;
constexpr uint8_t PWM_BITS = 10;
constexpr uint16_t PWM_MAX = (1U << PWM_BITS) - 1;

uint8_t goldPercent = 0;
uint8_t silverPercent = 0;
bool goldAttached = false;
bool silverAttached = false;
bool pwmReady = false;

char line[48];
size_t lineLength = 0;
bool discardLine = false;

void holdLow(uint8_t pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void stopWithError() {
  if (goldAttached) {
    ledcDetach(GOLD_PIN);
  }
  if (silverAttached) {
    ledcDetach(SILVER_PIN);
  }
  holdLow(GOLD_PIN);
  holdLow(SILVER_PIN);
  pwmReady = false;
  Serial.println("ERR PWM disabled; check both outputs and reset");
}

uint16_t pwmCount(uint8_t percent) {
  return (static_cast<uint32_t>(percent) * PWM_MAX + 50U) / 100U;
}

void printStatus() {
  Serial.print("Gold ");
  Serial.print(goldPercent);
  Serial.print("%, silver ");
  Serial.print(silverPercent);
  Serial.println("% (PWM set to 200 Hz)");
}

void setDuty(uint8_t gold, uint8_t silver) {
  if (!ledcWrite(GOLD_PIN, pwmCount(gold)) ||
      !ledcWrite(SILVER_PIN, pwmCount(silver))) {
    stopWithError();
    return;
  }
  goldPercent = gold;
  silverPercent = silver;
  printStatus();
}

bool parsePercent(const char *token, uint8_t &percent) {
  if (token == nullptr) {
    return false;
  }
  unsigned value = 0;
  for (; *token != '\0'; ++token) {
    if (*token < '0' || *token > '9') {
      return false;
    }
    value = value * 10 + (*token - '0');
    if (value > 100) {
      return false;
    }
  }
  percent = value;
  return true;
}

void handleLine() {
  constexpr char separators[] = " \t";
  char *next = nullptr;
  const char *command = strtok_r(line, separators, &next);
  if (command == nullptr) {
    return;
  }
  if (!pwmReady) {
    Serial.println("ERR PWM disabled; reset required");
    return;
  }

  const char *first = strtok_r(nullptr, separators, &next);
  const char *second = strtok_r(nullptr, separators, &next);
  const char *extra = strtok_r(nullptr, separators, &next);
  uint8_t gold = goldPercent;
  uint8_t silver = silverPercent;

  if (strcmp(command, "status") == 0 && first == nullptr) {
    printStatus();
  } else if (strcmp(command, "off") == 0 && first == nullptr) {
    setDuty(0, 0);
  } else if (strcmp(command, "g") == 0 && second == nullptr &&
             parsePercent(first, gold)) {
    setDuty(gold, silver);
  } else if (strcmp(command, "s") == 0 && second == nullptr &&
             parsePercent(first, silver)) {
    setDuty(gold, silver);
  } else if (strcmp(command, "set") == 0 && extra == nullptr &&
             parsePercent(first, gold) && parsePercent(second, silver)) {
    setDuty(gold, silver);
  } else {
    Serial.println("ERR use g 0..100, s 0..100, set 0..100 0..100, off, or status");
  }
}
}  // namespace

void setup() {
  holdLow(GOLD_PIN);
  holdLow(SILVER_PIN);
  Serial.begin(115200);

  goldAttached = ledcAttach(GOLD_PIN, PWM_HZ, PWM_BITS);
  silverAttached = ledcAttach(SILVER_PIN, PWM_HZ, PWM_BITS);
  if (!goldAttached || !silverAttached) {
    stopWithError();
    return;
  }
  pwmReady = true;
  setDuty(0, 0);
}

void loop() {
  while (Serial.available() > 0) {
    const int byteRead = Serial.read();
    if (byteRead == '\n' || byteRead == '\r') {
      if (discardLine) {
        Serial.println("ERR invalid input or line too long");
      } else if (lineLength != 0) {
        line[lineLength] = '\0';
        handleLine();
      }
      lineLength = 0;
      discardLine = false;
    } else if (byteRead == 0 || lineLength >= sizeof(line) - 1) {
      discardLine = true;
    } else if (!discardLine) {
      line[lineLength++] = static_cast<char>(byteRead);
    }
  }
}

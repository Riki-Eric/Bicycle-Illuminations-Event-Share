// 翼A Rev E。J5-1:12 V、J5-2:DIN（GPIO18から5 V変換）、J5-3:GND。
// J10/J11は1:3.3 V、2:OUT（GPIO34/35）、3:GND。ESP32 Dev Module。
// 39番の開始を受けた後、左右のパルス累積で翼を伸ばし、27番へRPMを送る。
// 115200 bps・改行あり。helpは操作一覧、statusはGPIO・受理パルス・RPM・進捗・設定・通信と出力状態。
// count zeroで計測の基準を記録し、countでA/Bの増分を表示。公演の回転数には影響しない。
// watch 1000で定期表示、watch offで停止。config / config ppr_a / config exportで保存値確認。
// 39番を停止してshow main / show shortで編集先を選ぶ。difficulty A 2 16は2回転毎に発射・16回転分で達成。
// shot A 2は間隔のみ変更。rate A 150で累積倍率、trim 15 25で端を消灯、fで左右反転を保存。
// goal <Aパルス> <Bパルス>は本披露のゴール値。Nano 45番で試走を記録し、換算後の値を停止中に入力する。
// Sitesの走行中倍率は、この公演の次の受理パルスから基本倍率に掛ける。既に溜まった進捗は維持する。
// test a / test bは片側だけ。test finale / test shortはゴールの見本を低輝度で表示。stopですぐ消灯。
// mark a root / mark a tip / mark b root / mark b tip で付け根・有効な先端を低輝度で4秒表示。
// mapで反転・消灯幅を反映したDINからの番号を確認。A/Bの回転計を片側ずつ回してcountの増分を照合する。
// WING_USE_ARDUINO_BRIDGE は 0: Rev E、1: 予備機D8、2: 予備機A0。
// 予備機はXIAO_ESP32C3を選択。A=D4(GPIO6)、B=D5(GPIO7)、ArduinoへはD10(GPIO10)から反転UART。
// XIAOの標準はD8版。A0版はWING_USE_ARDUINO_BRIDGEを2にする。USB CDC On BootはEnabled。
// DevKitCで予備機を使う場合はGPIO18から送る。配線は翼AのREADMEを参照。
#ifndef WING_USE_ARDUINO_BRIDGE
#if defined(ARDUINO_XIAO_ESP32C3)
#define WING_USE_ARDUINO_BRIDGE 1
#else
#define WING_USE_ARDUINO_BRIDGE 0
#endif
#endif
static_assert(WING_USE_ARDUINO_BRIDGE >= 0 && WING_USE_ARDUINO_BRIDGE <= 2,
              "Choose 0 (Rev E), 1 (D8), or 2 (A0)");
#if defined(ARDUINO_XIAO_ESP32C3)
static_assert(WING_USE_ARDUINO_BRIDGE != 0, "XIAO requires an Arduino bridge: choose D8 or A0");
#endif
#include "src/OpeningShow/Addressable.h"
#include "src/OpeningShow/LocalControls.h"
#include "src/OpeningShow/ShowNode.h"
#include "src/OpeningShow/Settings.h"
#include "src/OpeningShow/WingFinale.h"
#include "src/RiderSpeedRadio.h"
#if WING_USE_ARDUINO_BRIDGE
#include "src/ArduinoWingOutput.h"
#endif
#include <esp_timer.h>

using namespace OpeningShow;
#if defined(ARDUINO_XIAO_ESP32C3)
constexpr uint8_t SENSOR[] = {D4, D5};
#else
constexpr uint8_t SENSOR[] = {34, 35};
#endif
constexpr uint16_t PPR[] = {6, 6}; // スリット円板1回転で6パルス。ペダルとの比は試走で確認。
constexpr uint16_t WING_PIXELS = 100, HALF = WING_PIXELS / 2;
constexpr uint16_t STRIP_CM = 500;
constexpr bool FLIP_WINGS = false;
constexpr uint32_t MIN_GAP_US = 1500, STOP_US = 1200000;
constexpr uint32_t MAX_PULSES = 1000000, MAX_RATE_PERCENT = 1000;
enum WingSetting { Brightness, ShotMs, ShotTail, RgbOrder, PprA, PprB };
Setting options[] = {
  {"brightness", 128, 0, 255}, {"shot_ms", 600, 100, 5000}, {"shot_tail", 3, 1, HALF},
  {"rgb", 0, 0, 1}, {"ppr_a", PPR[0], 1, 1000}, {"ppr_b", PPR[1], 1, 1000},
};
Settings config("tbt-wing-fx", options);
struct Difficulty {
  uint32_t goal[2];
  uint32_t shot[2];
};
struct Difficulties {
  Difficulty main;
  Difficulty shortRide;
};
// 本披露の仮目標は各135回転（センサー軸60 RPMで2分15秒）。
// 円板:ペダル=1:1なら、80 rpm × 2分15秒でAは180回転。BはAの1.10倍。
Difficulties difficulties{{{180 * PPR[0], 198 * PPR[1]}, {3 * PPR[0], 3 * PPR[1]}},
                          {{320, 288}, {60, 36}}};
bool editShort = false;
uint32_t rates[] = {100, 100};
uint32_t trimCm[2]{};
uint8_t flipWings = FLIP_WINGS;
enum class Preview : uint8_t { Off, Sides, Trim, MainFinale, ShortGoal, SideA, SideB, Mark };
uint16_t markPixel = 0;
Preview preview = Preview::Off;
constexpr uint8_t PREVIEW_BRIGHTNESS = 48;

struct Pulse {
  int64_t last = 0;
  uint32_t period = 0;
  uint32_t total = 0;
};
struct Wing {
  uint32_t baseline = 0;
  uint32_t previous = 0;
  uint64_t weighted = 0;
  uint32_t shotCount = 0, shotAt = 0, reachedAt = 0;
  uint8_t percent = 0;
  bool shooting = false, reached = false;
};
Pulse pulses[2];
Wing wings[2];
portMUX_TYPE pulseLock = portMUX_INITIALIZER_UNLOCKED;
#if WING_USE_ARDUINO_BRIDGE
static_assert(WING_PIXELS == WingBridge::Pixels);
#if defined(ARDUINO_XIAO_ESP32C3)
constexpr uint8_t BRIDGE_TX = D10;
ArduinoWingOutput strip(Serial1, BRIDGE_TX);
#else
constexpr uint8_t BRIDGE_TX = 18;
ArduinoWingOutput strip(Serial2, BRIDGE_TX);
#endif
#else
Addressable<WING_PIXELS> strip(18, false);
#endif
ShowNode node;
RideRate rideRate;
Console console;
ConsoleWatch watch;
uint32_t countBase[2]{};
bool fault = false, lit = false, riding = false;
uint32_t lastFrame = 0, testAt = 0, lastRpm = 0, rideRun = 0;

constexpr uint8_t progressPercent(uint64_t weighted, uint32_t target) {
  const uint64_t percent = weighted / (uint64_t(target) * 100);
  return percent >= 100 ? 100 : uint8_t(percent);
}
static_assert(progressPercent(50 * 100 * 100, 100) == 50);
static_assert(progressPercent(50 * 100 * 100 + 10 * 100 * 150, 100) == 65);
static_assert(progressPercent(2 * 150 * 100, 3) == 100);

uint16_t pulsesPerRev(uint8_t side) { return config[side ? PprB : PprA]; }
constexpr uint16_t trimPixels(uint32_t cm) {
  return (cm * WING_PIXELS + STRIP_CM / 2) / STRIP_CM;
}
static_assert(trimPixels(0) == 0 && trimPixels(12) == 2 && trimPixels(28) == 6);

void ARDUINO_ISR_ATTR onPulse(void *arg) {
  auto &pulse = *static_cast<Pulse *>(arg);
  const int64_t now = esp_timer_get_time();
  portENTER_CRITICAL_ISR(&pulseLock);
  const int64_t gap = now - pulse.last;
  if (!pulse.last || gap >= MIN_GAP_US) {
    if (!pulse.last || gap >= STOP_US) pulse.period = 0;
    else pulse.period = pulse.period ? (3 * pulse.period + uint32_t(gap)) / 4 : uint32_t(gap);
    pulse.last = now;
    ++pulse.total;
  }
  portEXIT_CRITICAL_ISR(&pulseLock);
}

uint16_t rpm(const Pulse &pulse, uint8_t side, int64_t now) {
  if (!pulse.period || now - pulse.last >= STOP_US) return 0;
  const uint32_t value = 600000000UL / (max(pulse.period, uint32_t(now - pulse.last)) * pulsesPerRev(side));
  return min(value, uint32_t(UINT16_MAX));
}

void stop() {
  preview = Preview::Off;
  riding = false;
  node.suppress();
  if (lit) fault |= !strip.clear();
  lit = false;
}

void printRates() {
  Serial.printf("rate A=%lu%% B=%lu%%\n", (unsigned long)rates[0], (unsigned long)rates[1]);
}

bool saveSetting(const char *key, const void *value, size_t size) {
  Preferences settings;
  if (!settings.begin("tbt-wing", false)) {
    Serial.println("save failed");
    return false;
  }
  const bool saved = settings.putBytes(key, value, size) == size;
  settings.end();
  Serial.println(saved ? "saved" : "save failed");
  return saved;
}

bool savePair(const char *key, uint32_t *values, uint32_t a, uint32_t b) {
  const uint32_t pair[] = {a, b};
  if (!saveSetting(key, pair, sizeof(pair))) return false;
  values[0] = a;
  values[1] = b;
  return true;
}

void loadPair(Preferences &settings, const char *key, uint32_t *values, uint32_t minimum, uint32_t limit) {
  uint32_t saved[2];
  if (settings.getBytesLength(key) == sizeof(saved) &&
      settings.getBytes(key, saved, sizeof(saved)) == sizeof(saved) &&
      saved[0] >= minimum && saved[0] <= limit && saved[1] >= minimum && saved[1] <= limit) {
    values[0] = saved[0];
    values[1] = saved[1];
  }
}

bool idle(Phase phase) {
  return phase == Phase::Off && node.idle(millis());
}

bool positive(const char *arg, uint32_t limit, uint32_t &value) {
  if (!arg || !arg[0] || arg[strspn(arg, "0123456789")]) return false;
  const unsigned long parsed = strtoul(arg, nullptr, 10);
  if (!parsed || parsed > limit) return false;
  value = parsed;
  return true;
}

int bikeIndex(const char *arg) {
  if (!arg || !arg[0] || arg[1]) return -1;
  if (arg[0] == 'A' || arg[0] == 'a') return 0;
  return arg[0] == 'B' || arg[0] == 'b' ? 1 : -1;
}

void setRate(const char *bike, const char *arg) {
  const int side = bikeIndex(bike);
  uint32_t percent;
  if (side < 0 || !positive(arg, MAX_RATE_PERCENT, percent)) {
    Serial.println("Use rate A 150 or rate B 75 (1..1000 percent)");
    return;
  }
  if (savePair("rate", rates, side == 0 ? percent : rates[0], side == 1 ? percent : rates[1]))
    printRates();
}

void printDifficulty(const Difficulty &profile, bool shortProfile, int side) {
  Serial.printf("edit=%s\n", shortProfile ? "short" : "main");
  for (uint8_t i = 0; i < 2; ++i) {
    if (side >= 0 && side != i) continue;
    Serial.printf("%c shot=%.2f rev (%lu pulses) goal=%.2f rev (%lu pulses)\n", 'A' + i,
        double(profile.shot[i]) / pulsesPerRev(i), (unsigned long)profile.shot[i],
        double(profile.goal[i]) / pulsesPerRev(i), (unsigned long)profile.goal[i]);
  }
}

void configureDifficulty(const char *action, const char *first, const char *second, const char *third) {
  Difficulties updated = difficulties;
  Difficulty &selected = editShort ? updated.shortRide : updated.main;
  const int side = bikeIndex(first);
  uint32_t a, b;
  if (side >= 0 && (!strcmp(action, "difficulty") || !strcmp(action, "shot"))) {
    const uint16_t ppr = pulsesPerRev(side);
    const uint32_t maxTurns = MAX_PULSES / ppr;
    const bool combined = !strcmp(action, "difficulty");
    if (!positive(second, maxTurns, a) ||
        (combined ? (!positive(third, maxTurns, b) || a > b) : third != nullptr)) {
      Serial.println("Use difficulty A 2 16 (1 <= shot <= goal) or shot A 2");
      return;
    }
    selected.shot[side] = a * ppr;
    if (combined) selected.goal[side] = b * ppr;
  } else {
    if (third || !positive(first, MAX_PULSES, a) || !positive(second, MAX_PULSES, b)) {
      Serial.println("Use difficulty A 2 16, shot A 2 or goal/short/shot <A pulses> <B pulses>");
      return;
    }
    if (!strcmp(action, "goal")) {
      updated.main.goal[0] = a;
      updated.main.goal[1] = b;
    } else if (!strcmp(action, "short")) {
      updated.shortRide.goal[0] = a;
      updated.shortRide.goal[1] = b;
    } else if (!strcmp(action, "shot")) {
      updated.main.shot[0] = updated.shortRide.shot[0] = a;
      updated.main.shot[1] = updated.shortRide.shot[1] = b;
    } else {
      Serial.println("Use difficulty A 2 16 with a bike letter");
      return;
    }
  }
  // Commit both profiles together, including legacy pulse commands that update both.
  if (saveSetting("difficulty", &updated, sizeof(updated))) {
    difficulties = updated;
    if (side >= 0) printDifficulty(selected, editShort, side);
  }
}

void printTrim() {
  Serial.printf("trim DIN=%lu cm (%u groups) END=%lu cm (%u groups); flip=%s\n",
      (unsigned long)trimCm[0], trimPixels(trimCm[0]),
      (unsigned long)trimCm[1], trimPixels(trimCm[1]), flipWings ? "on" : "off");
}

bool trimWidth(const char *arg, uint32_t &cm) {
  if (!strcmp(arg, "0")) { cm = 0; return true; }
  return positive(arg, STRIP_CM / 2, cm) && trimPixels(cm) < HALF;
}

void configureTrim(const char *first, const char *second, uint32_t now) {
  if (!second && !strcmp(first, "run")) {
    preview = Preview::Off;
    return;
  }
  if (second || strcmp(first, "test")) {
    uint32_t a, b;
    if (!trimWidth(first, a) || (second && !trimWidth(second, b))) {
      Serial.println("Use trim 20, trim 15 25, trim test or trim run; leave one group per wing");
      return;
    }
    if (!savePair("trim", trimCm, a, second ? b : a)) return;
    printTrim();
  }
  if (!fault) { preview = Preview::Trim; testAt = now; }
}

uint16_t wingMarker(uint8_t side, bool tip) {
  const uint8_t physicalSide = side ^ flipWings;
  const uint16_t distance = tip ? HALF - trimPixels(trimCm[physicalSide]) - 1 : 0;
  return physicalSide ? HALF + distance : HALF - 1 - distance;
}

void printMap() {
  Serial.printf("DIN positions (1-based) A root=%u tip=%u; B root=%u tip=%u\n",
      unsigned(wingMarker(0, false) + 1), unsigned(wingMarker(0, true) + 1),
      unsigned(wingMarker(1, false) + 1), unsigned(wingMarker(1, true) + 1));
}

void help() {
#if WING_USE_ARDUINO_BRIDGE
  Serial.printf("43 Wing / %s bridge (GPIO%u inverted UART) | 115200 bps, newline\n",
      WING_USE_ARDUINO_BRIDGE == 2 ? "A0" : "D8", unsigned(BRIDGE_TX));
#else
  Serial.println("43 Wing / Rev E direct (GPIO18 WS2811) | 115200 bps, newline");
#endif
  Serial.printf("status: sensors, progress and settings | count zero then count: measure A=GPIO%u / B=GPIO%u\n",
      unsigned(SENSOR[0]), unsigned(SENSOR[1]));
  Serial.println("map: DIN positions | OFF: mark a root / mark b tip (a/b, root/tip), 4s | stop: blackout");
  Serial.println("show [main|short] | difficulty [A|B shot goal] | shot [A|B turns]");
  Serial.println("goal <A pulses> <B pulses> | short <A pulses> <B pulses> | rate [A|B percent] | trim | f");
  Serial.println("test [a|b|finale|short] | stop | help");
  ConsoleWatch::help();
  Settings::help();
}

void printInputs(const Pulse *p, Phase phase, uint32_t now) {
  const auto us = esp_timer_get_time();
  const uint16_t a = rpm(p[0], 0, us), b = rpm(p[1], 1, us);
  Serial.printf("phase=%s link=%u out=%u fault=%u GPIO A=%u B=%u RPM A=%u.%u B=%u.%u progress A=%u%% B=%u%%\n",
      phaseLabel(phase), node.follow.linked(now), lit, fault, digitalRead(SENSOR[0]), digitalRead(SENSOR[1]),
      a / 10U, a % 10U, b / 10U, b % 10U, riding ? wings[0].percent : 0, riding ? wings[1].percent : 0);
}

void printCount(const Pulse *p) {
  Serial.printf("pulses total A=%lu B=%lu; since count zero A=%lu B=%lu\n",
      (unsigned long)p[0].total, (unsigned long)p[1].total,
      (unsigned long)(p[0].total - countBase[0]), (unsigned long)(p[1].total - countBase[1]));
}

void configure(const char *cmd, Phase phase, const Pulse *p, uint32_t now) {
  if (!strcmp(cmd, "help")) { help(); return; }
  if (watch.command(cmd, now) || config.command(cmd, idle(phase) && preview == Preview::Off)) return;
  char buffer[48];
  strlcpy(buffer, cmd, sizeof(buffer));
  char *context = nullptr;
  const char *action = strtok_r(buffer, " \t", &context);
  if (!action) return;
  const char *first = strtok_r(nullptr, " \t", &context);
  const char *second = strtok_r(nullptr, " \t", &context);
  const char *third = strtok_r(nullptr, " \t", &context);
  if (strtok_r(nullptr, " \t", &context)) {
    Serial.println("ERR too many arguments");
    return;
  }
  const bool rateCommand = !strcmp(action, "rate");
  const bool difficultyCommand = !strcmp(action, "difficulty");
  const bool shotCommand = !strcmp(action, "shot");
  const bool showCommand = !strcmp(action, "show");
  const bool trimCommand = !strcmp(action, "trim");
  const bool flipCommand = !strcmp(action, "f");
  if (!first && !strcmp(action, "status")) {
    watch.printed(now);
    printInputs(p, phase, now);
    printCount(p);
    printRates();
    printDifficulty(difficulties.main, false, -1);
    printDifficulty(difficulties.shortRide, true, -1);
    Serial.printf("selected profile=%s\n", editShort ? "short" : "main");
    printTrim();
    printMap();
  } else if (!first && !strcmp(action, "map")) {
    printTrim();
    printMap();
  } else if (!first && !strcmp(action, "help")) {
    help();
  } else if (!strcmp(action, "count") && !second && (!first || !strcmp(first, "zero"))) {
    if (first) { countBase[0] = p[0].total; countBase[1] = p[1].total; }
    printCount(p);
  } else if (rateCommand && !first) {
    printRates();
  } else if ((!second && (difficultyCommand || shotCommand) && (!first || bikeIndex(first) >= 0)) ||
             (showCommand && !first)) {
    printDifficulty(editShort ? difficulties.shortRide : difficulties.main, editShort, bikeIndex(first));
  } else if (!first && trimCommand) {
    printTrim();
  } else if (!first && !strcmp(action, "stop")) {
    stop();
  } else if (!idle(phase)) {
    Serial.println("ERR stop the show on 39 and edit while idle");
  } else if (!strcmp(action, "mark")) {
    const int side = bikeIndex(first);
    if (fault || !node.idle(now) || preview != Preview::Off || side < 0 || !second || third ||
        (strcmp(second, "root") && strcmp(second, "tip"))) {
      Serial.println("ERR use mark a root or mark b tip while 39 is OFF and no test is running");
      return;
    }
    markPixel = wingMarker(side, !strcmp(second, "tip"));
    preview = Preview::Mark;
    testAt = now;
    Serial.printf("43 wing %c %s: DIN position %u, 4s\n", 'A' + side, second, unsigned(markPixel + 1));
  } else if (showCommand && !second && (!strcmp(first, "main") || !strcmp(first, "short"))) {
    editShort = !strcmp(first, "short");
    printDifficulty(editShort ? difficulties.shortRide : difficulties.main, editShort, -1);
  } else if (rateCommand && !third) {
    setRate(first, second);
  } else if (difficultyCommand || shotCommand || !strcmp(action, "goal") || !strcmp(action, "short")) {
    configureDifficulty(action, first, second, third);
  } else if (trimCommand && !third) {
    configureTrim(first, second, now);
  } else if (flipCommand && !first) {
    const uint8_t updated = !flipWings;
    if (saveSetting("flip", &updated, sizeof(updated))) { flipWings = updated; printTrim(); }
  } else if (!second && !strcmp(action, "test") && !fault && node.idle(now) &&
             (!first || bikeIndex(first) >= 0 || !strcmp(first, "finale") || !strcmp(first, "short"))) {
    preview = !first ? Preview::Sides :
        bikeIndex(first) == 0 ? Preview::SideA : bikeIndex(first) == 1 ? Preview::SideB :
        !strcmp(first, "finale") ? Preview::MainFinale : Preview::ShortGoal;
    testAt = now;
  } else {
    help();
  }
}

uint32_t render(uint16_t pixel, Phase phase, uint32_t age, uint32_t now, bool shortShow) {
  if (preview == Preview::Mark)
    return pixel == markPixel ? scale(0xffffff, min(config[Brightness], uint32_t{PREVIEW_BRIGHTNESS})) : 0;
  const uint8_t physicalSide = pixel >= HALF;
  const uint16_t distance = physicalSide ? pixel - HALF : HALF - 1 - pixel;
  const uint16_t span = HALF - trimPixels(trimCm[physicalSide]);
  if (distance >= span) return 0;
  const uint8_t side = physicalSide ^ flipWings;
  if (preview == Preview::MainFinale || preview == Preview::ShortGoal) {
    const uint32_t testAge = now - testAt;
    const uint32_t testColor = preview == Preview::MainFinale ?
        WingFinale::mainPixel(distance, span, testAge, WHITE_MS) :
        WingFinale::shortPixel(distance, span, testAge);
    return scale(testColor, min(config[Brightness], uint32_t{PREVIEW_BRIGHTNESS}));
  }
  if (preview == Preview::Trim && distance + 1 == span) return 0x303030;
  if ((preview == Preview::SideA && side != 0) || (preview == Preview::SideB && side != 1)) return 0;
  if (preview != Preview::Off) return side ? 0x000510 : 0x100600;
  const Wing &w = wings[side];
  constexpr uint32_t colors[] = {0x0b5cff, 0x08debd, 0xffb032, 0xff4728, 0xffb423};
  uint32_t color = 0;
  if (phase == Phase::Ride || phase == Phase::TreeCharge) {
    const uint8_t percent = phase == Phase::TreeCharge ? 100 : w.percent;
    const uint16_t length = (percent * span + 99) / 100;
    if (distance < length) color = colors[percent / 25];
    const uint32_t shotMs = config[ShotMs];
    if (w.shooting && now - w.shotAt < shotMs) {
      const int head = (now - w.shotAt) * max(0, int(length) - 1) / max(uint32_t{1}, shotMs - 40);
      if (distance <= head && uint32_t(head - distance) < config[ShotTail]) color = rgb(180, 232, 255);
    }
    if (shortShow && w.reached && now - w.reachedAt < WingFinale::SHORT_MS)
      color = WingFinale::shortPixel(distance, span, now - w.reachedAt);
  } else if (phase == Phase::White) {
    color = WingFinale::mainPixel(distance, span, age, node.follow.packet().duration);
  } else if (phase == Phase::ShortHold && w.reached) {
    color = WingFinale::shortPixel(distance, span, now - w.reachedAt);
  } else if (phase == Phase::Steady) {
    color = scale(WingFinale::GOLD, 96);
  } else if (phase >= Phase::Sparkle) {
    color = WingFinale::GOLD;
    if (phase == Phase::Fade) {
      const uint32_t duration = node.follow.packet().duration;
      color = scale(color, age < duration ? (duration - age) * 255 / duration : 0);
    }
  }
  return scale(color, config[Brightness]);
}

bool validDifficulty(const Difficulty &profile) {
  for (uint8_t i = 0; i < 2; ++i) {
    if (!profile.goal[i] || profile.goal[i] > MAX_PULSES ||
        !profile.shot[i] || profile.shot[i] > MAX_PULSES) return false;
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  config.begin();
  for (uint8_t i = 0; i < 2; ++i) {
    pinMode(SENSOR[i], INPUT);
    attachInterruptArg(SENSOR[i], onPulse, &pulses[i], FALLING);
  }
  Preferences settings;
  if (settings.begin("tbt-wing", true)) {
    Difficulties saved;
    if (settings.getBytesLength("difficulty") == sizeof(saved) &&
        settings.getBytes("difficulty", &saved, sizeof(saved)) == sizeof(saved) &&
        validDifficulty(saved.main) && validDifficulty(saved.shortRide)) {
      difficulties = saved;
    } else {
      loadPair(settings, "goal", difficulties.main.goal, 1, MAX_PULSES);
      loadPair(settings, "short", difficulties.shortRide.goal, 1, MAX_PULSES);
      loadPair(settings, "shot", difficulties.main.shot, 1, MAX_PULSES);
      memcpy(difficulties.shortRide.shot, difficulties.main.shot, sizeof(Difficulty::shot));
    }
    loadPair(settings, "rate", rates, 1, MAX_RATE_PERCENT);
    loadPair(settings, "trim", trimCm, 0, (STRIP_CM / 2) - 1);
    if (trimPixels(trimCm[0]) >= HALF || trimPixels(trimCm[1]) >= HALF) trimCm[0] = trimCm[1] = 0;
    uint8_t savedFlip;
    if (settings.getBytesLength("flip") == sizeof(savedFlip) &&
        settings.getBytes("flip", &savedFlip, sizeof(savedFlip)) == sizeof(savedFlip) &&
        savedFlip <= 1) flipWings = savedFlip;
    settings.end();
  }
  fault = !strip.begin();
  node.begin(Unit::Wing, Unit::Wing);
  static_assert(CHANNEL == RiderSpeedRadio::CHANNEL);
  help();
  printRates();
  printMap();
}

void loop() {
  const uint32_t now = millis();
  if (node.receive(now, &rideRate)) stop();
  Pulse measured[2];
  portENTER_CRITICAL(&pulseLock);
  measured[0] = pulses[0];
  measured[1] = pulses[1];
  portEXIT_CRITICAL(&pulseLock);
  if (const char *cmd = console.read()) configure(cmd, node.phase(now), measured, now);
  const Phase phase = node.phase(now);
  const auto &cue = node.follow.packet();
  const bool shortShow = cue.flags & Short;
  if (preview != Preview::Off && (!node.idle(now) || now - testAt >= (preview == Preview::Trim ? 15000U : 4000U)))
    preview = Preview::Off;
  const bool ridingPhase = phase == Phase::Ride || phase == Phase::TreeCharge;
  if (ridingPhase && (!riding || rideRun != cue.run)) {
    rideRun = cue.run;
    for (uint8_t i = 0; i < 2; ++i) {
      wings[i] = {};
      wings[i].baseline = wings[i].previous = measured[i].total;
    }
  }
  riding = ridingPhase;
  const Difficulty &difficulty = shortShow ? difficulties.shortRide : difficulties.main;
  if (riding) for (uint8_t i = 0; i < 2; ++i) {
    Wing &w = wings[i];
    const uint32_t count = measured[i].total - w.baseline;
    const uint16_t liveRate = rideRate.boot == cue.boot && rideRate.run == cue.run ? rideRate.value[i] : 100;
    if (phase == Phase::Ride && !w.reached) {
      w.weighted += uint64_t(measured[i].total - w.previous) * rates[i] * liveRate;
      w.percent = progressPercent(w.weighted, difficulty.goal[i]);
      if (w.percent == 100) { w.reachedAt = now; w.reached = true; }
    }
    w.previous = measured[i].total;
    const uint32_t shot = count / difficulty.shot[i];
    if (shot != w.shotCount) { w.shotCount = shot; w.shotAt = now; w.shooting = true; }
  }
  if (!fault && !node.muted(now) && (preview != Preview::Off || phase >= Phase::Ride)) {
    if (now - lastFrame >= 40) {
      lastFrame = now;
      strip.setRgbOrder(config[RgbOrder]);
      fault = !strip.draw([&](uint16_t pixel) -> uint32_t {
        return render(pixel, phase, node.follow.elapsed(now), now, shortShow);
      });
      lit = !fault;
      if (fault) strip.clear();
    }
  } else if (lit) {
    fault |= !strip.clear();
    lit = false;
  }
#if WING_USE_ARDUINO_BRIDGE
  if (fault) strip.clear();
  fault |= !strip.service(now);
#endif
  if (now - lastRpm >= 100) {
    lastRpm = now;
    const int64_t us = esp_timer_get_time();
    const RiderSpeedRadio::Packet packet{RiderSpeedRadio::PACKET_TAG,
        rpm(measured[0], 0, us), rpm(measured[1], 1, us)};
    node.radio.broadcast(&packet, sizeof(packet));
    // Ride telemetry complements the original two-number packet used by the display bars.
    RideSample sample;
    sample.boot = node.radio.boot();
    sample.run = node.follow.linked(now) ? cue.run : 0;
    sample.revision = riding && rideRate.boot == cue.boot && rideRate.run == cue.run ? rideRate.revision : 0;
    for (uint8_t i = 0; i < 2; ++i) {
      sample.pulses[i] = measured[i].total;
      sample.goal[i] = (phase == Phase::Off ? difficulties.main : difficulty).goal[i];
      sample.rpm[i] = i ? packet.rpmTenthsB : packet.rpmTenthsA;
      sample.applied[i] = sample.revision ? rideRate.value[i] : 100;
      sample.ppr[i] = pulsesPerRev(i);
      sample.baseRate[i] = rates[i];
    }
    node.radio.broadcast(&sample, sizeof(sample));
  }
  node.report(now, fault ? Fault : preview != Preview::Off ? 0 : Ready, riding ? wings[0].percent : 0,
              riding ? wings[1].percent : 0);
  if (watch.due(now)) { watch.printed(now); printInputs(measured, phase, now); }
  delay(2);
}

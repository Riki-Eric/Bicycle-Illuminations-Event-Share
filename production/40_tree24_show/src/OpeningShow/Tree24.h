#pragma once
#include "LocalControls.h"
#include "Settings.h"
#include "ShowNode.h"

namespace OpeningShow {
constexpr uint8_t TREE_GOLD[] = {23, 21, 18, 16};
constexpr uint8_t TREE_WHITE[] = {22, 19, 17, 4};
struct Duties { uint8_t gold, white; };
struct TreeAppearance {
  uint8_t running, gold, white, sparkle, step;
  uint16_t sparkleMs;
};
constexpr uint8_t treeThreshold(uint8_t branch, uint8_t step) {
  // TreeChargeの進捗0..100に正規化。step_pctは段の間隔の偏りを調整し、最上段は常に100%。
  constexpr uint8_t accelerated[] = {20, 38, 54, 68, 80, 91, 100};
  if (branch >= 7) return 100;
  const uint16_t linear = ((branch + 1) * 100 + 3) / 7;
  const uint16_t shape = step < 1 ? 0 : step > 14 ? 13 : step - 1;
  return linear + ((accelerated[branch] - linear) * shape + 4) / 9;
}
static_assert(treeThreshold(0, 10) == 20 && treeThreshold(6, 10) == 100);
constexpr Duties treeDuties(uint8_t branch, Phase phase, uint32_t elapsed, uint8_t progress,
                            uint32_t duration, TreeAppearance style) {
  if (branch >= 7) return {0, 0};
  // 翼のRide中は消灯。cue.reserved由来の専用進捗をTreeChargeだけで使う。
  if (phase == Phase::TreeCharge)
    return {uint8_t(progress >= treeThreshold(branch, style.step) ? style.running : 0), 0};
  const uint8_t background = style.white / 28;
  if (phase == Phase::White) {
    if (!duration || elapsed >= duration) return {style.gold, background};
    const uint32_t front = elapsed * (7 * 256UL) / duration;
    const uint32_t position = branch * 256UL;
    const uint32_t distance = front >= position ? front - position : position - front;
    // 全段に薄い白を重ね、隣接段へ白を受け渡す。7段合計の白指令はwhite以下に保つ。
    const uint8_t white = background +
        (distance < 256 ? (style.white - 7 * background) * (256 - distance) / 256 : 0);
    return {style.gold, white};
  }
  if (phase == Phase::Sparkle) {
    const uint16_t period = style.sparkleMs < 2 ? 2 : style.sparkleMs;
    const uint32_t wave = elapsed / period;
    const uint32_t step = elapsed % period;
    const uint32_t fall = period - step;
    // 背景白を含めた7段合計をmax(white,sparkle)以下にし、個別設定の上限を守る。
    const uint8_t limit = style.white > style.sparkle ? style.white : style.sparkle;
    const uint8_t room = limit - 7 * background;
    const uint8_t accent = style.sparkle < room ? style.sparkle : room;
    const uint8_t peak = (step < fall ? step : fall) * accent / (period / 2);
    return {style.gold, uint8_t(background + (branch == (wave * 3) % 7 ? peak : 0))};
  }
  if (phase == Phase::Steady) return {style.running, 0};
  if (phase == Phase::Drape || phase == Phase::Hold) return {style.gold, background};
  return {0, 0};
}

constexpr uint8_t treeEase(uint8_t value) {
  return uint32_t(value) * value * (3 * 255UL - 2 * value) / (255UL * 255UL);
}
constexpr Duties plazaDuties(uint8_t branch, uint8_t count, uint32_t age,
                              uint8_t gold, uint8_t white, uint32_t ramp, uint8_t effect) {
  if (!count || branch >= count) return {0, 0};
  const uint32_t delay = ramp * branch / (2 * count);
  const uint32_t span = ramp - delay;
  const uint8_t level = !ramp || age >= ramp ? 255 : age <= delay ? 0 :
      treeEase((age - delay) * 255 / span);
  if (effect == 1) {
    // 比較用の8秒周期。設定した金・白の上限を超えず、金を保ちながら白を重ねる。
    const uint32_t position = (age % 8000 + uint32_t(branch) * 800 / count) % 8000;
    const uint32_t triangle = position <= 4000 ? position : 8000 - position;
    const uint8_t mix = treeEase(triangle * 255 / 4000);
    return {uint8_t(uint32_t(gold) * (255 - mix / 3) * level / (255UL * 255UL)),
            uint8_t(uint32_t(white) * mix * level / (255UL * 255UL))};
  }
  return {uint8_t(uint32_t(gold) * level / 255), uint8_t(uint32_t(white) * level / 255)};
}

class Tree24 {
 public:
  explicit Tree24(bool plaza) : plaza_(plaza),
      config_(plaza ? "tbt-plaza-fx" : "tbt-tree-fx", plaza ? plazaSettings_ : treeSettings_,
              plaza ? sizeof(plazaSettings_) / sizeof(Setting) : sizeof(treeSettings_) / sizeof(Setting)) {}
  void begin() {
    Serial.begin(115200);
    config_.begin();
    lights_.begin();
    local_.begin(34);
    for (uint8_t pin : TREE_GOLD) init(pin);
    for (uint8_t pin : TREE_WHITE) init(pin);
    if (plaza_) node_.begin(Unit::Plaza1, Unit::Plaza3);
    else node_.begin(Unit::TreeLower, Unit::TreeUpper);
    if (plaza_) {
      Preferences settings;
      if (settings.begin("tbt-plaza", true)) {
        const uint8_t count = settings.getUChar("branches", 0);
        if (count >= 1 && count <= 4) plazaBranches_ = count;
        settings.end();
      }
    }
    status(millis());
    help();
  }

  void tick() {
    const uint32_t now = millis();
    if (node_.receive(now)) stop();
    if (const char *cmd = console_.read()) command(cmd, now);
    if (local_.pressed(now)) {
      if (preview_ != Preview::None || isActive(node_.phase(now))) stop();
      else if (node_.idle(now)) start(now, Preview::Gold);
    }
    if (!node_.idle(now) || (preview_ == Preview::Colors && now - manualAt_ >= 4000))
      preview_ = Preview::None;
    const auto &cue = node_.follow.packet();
    const Phase phase = node_.phase(now);
    const uint8_t progress = cue.reserved > 100 ? 100 : cue.reserved;
    const uint8_t count = branches();
    const bool reverse = !plaza_ && config_[TreeReverse];
    const uint32_t age = node_.follow.elapsed(now);
    if (plaza_) {
      if (phase == Phase::Off || phase == Phase::Armed) plazaHeldRun_ = 0;
      else if (phase == Phase::Hold && !fault_ && !node_.muted(now)) plazaHeldRun_ = cue.run;
    }
    for (uint8_t i = 0; i < 4; ++i) {
      Duties duty{};
      if (i < count && !fault_ && !node_.muted(now)) {
        const uint8_t branch = stageFor(i, count, reverse);
        if (preview_ != Preview::None) {
          if (preview_ != Preview::Colors || !testBranch_ || testBranch_ == i + 1)
            duty = previewDuty(branch, now - manualAt_);
        } else if (plaza_) {
          // 開演前のSteadyでは外周を先に点けず、Holdを実行した回だけ穏やかな金へ移す。
          if ((phase == Phase::Hold || (phase == Phase::Steady && cue.run && plazaHeldRun_ == cue.run)) &&
              (node_.unit() != Unit::Plaza3 || (cue.flags & ThreePlazas))) {
            duty = phase == Phase::Steady ? Duties{uint8_t(config_[PlazaGold] / 4), 0} :
                                           plazaDuty(age, i, count);
          }
        } else {
          duty = treeDuties(branch, phase, age, progress, cue.duration, appearance());
        }
      }
      write(i, duty);
    }
    node_.report(now, fault_ ? Fault : preview_ != Preview::None ? 0 : Ready);
    const bool link = node_.follow.linked(now);
    lights_.update(node_.assigned() && !fault_, link, lit_, fault_ || !node_.radio.ready());
    if (watch_.due(now)) status(now);
    lit_ = false;
    delay(2);
  }

 private:
  enum class Preview : uint8_t { None, Gold, Colors, Sweep };
  enum TreeSetting { Running, Gold, White, Sparkle, Step, SparkleMs, TreeReverse };
  enum PlazaSetting { PlazaGold, PlazaWhite, Ramp, PlazaEffect };

  void help() const {
    Serial.println(plaza_ ? "41 Plaza / Tree24 A3 | 115200 bps, newline" : "40 Tree / Tree24 A3 | 115200 bps, newline");
    Serial.println(plaza_ ? "id [1..3]: box | branches [1..4]: assigned outputs | map: assigned terminals" :
                           "id [1=lower|2=upper]: box | map: terminals and stages from base");
    Serial.println("status: ID, link, faults, PWM | OFF: test 2 = T2 gold 2s then white 2s | test = all assigned | stop: blackout");
    Serial.println(plaza_ ? "p: plaza preview, stop to end" : "p: gold preview | p sweep: white sweep preview | stop to end");
    ConsoleWatch::help();
    Settings::help();
    if (plaza_) Serial.println("effect 0: soft spread/hold; 1: gold/white wave (white sets its limit)");
    else Serial.println("TreeCharge: step_pct shapes 7 steps ending at 100%; Ride: off; Steady: running gold");
  }
  void command(const char *cmd, uint32_t now) {
    const bool idle = node_.idle(now) && preview_ == Preview::None;
    if (watch_.command(cmd, now) || config_.command(cmd, idle)) return;
    if (!strcmp(cmd, "status")) status(now);
    else if (!strcmp(cmd, "map")) printMap();
    else if (!strcmp(cmd, "id")) Serial.printf("id=%u (0=unassigned)\n", node_.localId());
    else if (plaza_ && !strcmp(cmd, "branches")) Serial.printf("branches=%u\n", branches());
    else if (!strcmp(cmd, "stop")) stop();
    else if (idle && !strncmp(cmd, "id ", 3) && strlen(cmd) == 4) {
      Serial.println(node_.select(cmd[3] - '0') ? "id saved" : "id refused");
      status(now);
    } else if (plaza_ && idle && !strncmp(cmd, "branches ", 9) && strlen(cmd) == 10 &&
               cmd[9] >= '1' && cmd[9] <= '4') {
      Preferences settings;
      const uint8_t count = cmd[9] - '0';
      if (settings.begin("tbt-plaza", false) && settings.putUChar("branches", count) == 1) {
        plazaBranches_ = count;
        status(now);
      } else Serial.println("ERR save failed");
    } else if (node_.idle(now) && !strcmp(cmd, "p")) start(now, Preview::Gold);
    else if (!strcmp(cmd, "test") || !strncmp(cmd, "test ", 5)) {
      if (!node_.idle(now) || fault_ || !branches()) {
        Serial.println("ERR test requires 39 OFF, assigned ID and working PWM; status");
      } else if (!strcmp(cmd, "test")) {
        Serial.printf("T1..T%u: gold 2s, white 2s, then off\n", branches());
        start(now, Preview::Colors);
      } else if (strlen(cmd) == 6 && cmd[5] >= '1' && cmd[5] <= '0' + branches()) {
        const uint8_t terminal = cmd[5] - '0';
        Serial.printf("id=%u T%u only: gold 2s, white 2s, then off\n", node_.localId(), terminal);
        start(now, Preview::Colors, terminal);
      } else Serial.printf("ERR use test 1..%u for the assigned branches\n", branches());
    }
    else if (!plaza_ && node_.idle(now) && !strcmp(cmd, "p sweep")) start(now, Preview::Sweep);
    else help();
  }
  TreeAppearance appearance() const {
    return {uint8_t(config_[Running]), uint8_t(config_[Gold]), uint8_t(config_[White]),
            uint8_t(config_[Sparkle]), uint8_t(config_[Step]), uint16_t(config_[SparkleMs])};
  }
  Duties plazaDuty(uint32_t age, uint8_t branch, uint8_t count) const {
    return plazaDuties(branch, count, age, config_[PlazaGold], config_[PlazaWhite],
                       config_[Ramp], config_[PlazaEffect]);
  }
  Duties previewDuty(uint8_t branch, uint32_t age) const {
    if (preview_ == Preview::Colors) return age < 2000 ? Duties{12, 0} : Duties{0, 12};
    if (plaza_) return plazaDuty(age, branch, branches());
    if (preview_ == Preview::Sweep) {
      if (age < WHITE_MS) return treeDuties(branch, Phase::White, age, 100, WHITE_MS, appearance());
      if (age < WHITE_MS + SPARKLE_MS)
        return treeDuties(branch, Phase::Sparkle, age - WHITE_MS, 100, SPARKLE_MS, appearance());
    }
    return {uint8_t(config_[Gold]), 0};
  }

  void init(uint8_t pin) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
    if (!ledcAttach(pin, 200, 8) || !ledcWrite(pin, 0)) fault_ = true;
  }
  void write(uint8_t index, Duties duty) {
    const uint8_t pins[] = {TREE_GOLD[index], TREE_WHITE[index]};
    const uint8_t next[] = {duty.gold, duty.white};
    for (uint8_t ch = 0; ch < 2; ++ch) {
      if (duty_[index][ch] != next[ch]) {
        duty_[index][ch] = next[ch];
        if (!ledcWrite(pins[ch], next[ch])) {
          fault_ = true;
          ledcDetach(pins[ch]);
          digitalWrite(pins[ch], LOW);
          pinMode(pins[ch], OUTPUT);
        }
      }
    }
    lit_ |= duty.gold || duty.white;
  }
  uint8_t branches() const {
    if (!node_.assigned()) return 0;
    if (plaza_ && plazaBranches_) return plazaBranches_;
    switch (node_.unit()) {
      case Unit::TreeUpper:
      case Unit::Plaza2: return 3;
      case Unit::Plaza3: return 2;
      default: return 4;
    }
  }
  uint8_t stageFor(uint8_t index, uint8_t count, bool reverse) const {
    return (reverse ? count - 1 - index : index) + (node_.unit() == Unit::TreeUpper ? 4 : 0);
  }
  void printMap() const {
    const uint8_t count = branches();
    Serial.printf("%s id=%u\n", plaza_ ? "41 Plaza" : "40 Tree", node_.localId());
    if (!count) Serial.println("Unassigned; select id before testing");
    for (uint8_t i = 0; i < count; ++i) {
      Serial.printf("T%u: gold GPIO%u, white GPIO%u", i + 1, TREE_GOLD[i], TREE_WHITE[i]);
      if (!plaza_) Serial.printf(", stage %u/7 from base", stageFor(i, count, config_[TreeReverse]) + 1);
      Serial.println("");
    }
  }
  void stop() { preview_ = Preview::None; node_.suppress(); }
  void start(uint32_t now, Preview preview, uint8_t testBranch = 0) {
    if (!node_.assigned() || fault_) return;
    preview_ = preview;
    testBranch_ = testBranch;
    manualAt_ = now;
  }
  void status(uint32_t now) {
    watch_.printed(now);
    Serial.printf("%s id=%u branches=%u phase=%s link=%u radio=%u manual=%u pwmFault=%u"
                  " PWM gold/white T1=%u/%u T2=%u/%u T3=%u/%u T4=%u/%u\n",
        plaza_ ? "41 Plaza" : "40 Tree", node_.localId(), branches(), phaseLabel(node_.phase(now)), node_.follow.linked(now),
        node_.radio.ready(), preview_ != Preview::None, fault_, duty_[0][0], duty_[0][1],
        duty_[1][0], duty_[1][1], duty_[2][0], duty_[2][1], duty_[3][0], duty_[3][1]);
  }

  ShowNode node_;
  BoardLights lights_{32, 33, 25, 26};
  Contact local_;
  Console console_;
  ConsoleWatch watch_;
  uint8_t duty_[4][2]{};
  uint8_t plazaBranches_ = 0, testBranch_ = 0;
  uint32_t manualAt_ = 0, plazaHeldRun_ = 0;
  Preview preview_ = Preview::None;
  bool plaza_, fault_ = false, lit_ = false;
  Setting treeSettings_[7]{
    {"running", 18, 0, 255}, {"gold", 83, 0, 255}, {"white", 135, 0, 255},
    {"sparkle", 85, 0, 255}, {"step_pct", 10, 1, 14}, {"sparkle_ms", 370, 100, 3000},
    {"reverse", 0, 0, 1},
  };
  Setting plazaSettings_[4]{
    {"gold", 83, 0, 255}, {"white", 0, 0, 255}, {"ramp_ms", 1500, 0, 10000},
    {"effect", 0, 0, 1},
  };
  Settings config_;
};
}

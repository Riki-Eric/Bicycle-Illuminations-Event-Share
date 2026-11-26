// Bike-Drape Rev B2。J8制御5 V、J2軌跡5 V、J3:DATA/GND/5 V、J1照明12 V。
// J4は車体A、J5は車体B（本番は両方アップライト）。J6は人物照明。ESP32 Dev Module。
// 115200 bps・改行あり。helpで一覧。statusで進行/開始経路/各箱の受信経過・ID重複・準備状態。
// watch 1000で進行・到達率と車体/J6指令の定期表示、watch offで停止。site statusはSites送信状態。
// config / config trail_ms / config exportで設定確認。停止中にsetやdefaultで変更/項目ごとに復元。
// 走行中は回転数計のRPMで各車体PWMを上げる。frame_a / frame_bが上限、frame_rpm_a / frame_rpm_bが上限に達するRPM。
// 市長ボタンまたは準備後のLOCAL STARTで開始。軌跡の到着で両車を点灯し、人物照明J6は初期設定で消灯。
// 停止中のtest trail 0|1|2 / test after 0|1|2は低輝度で比較。set trail_fx / trail_after で採用。
// mark start / mark endは軌跡J3の演出始点／到着点だけを低輝度で4秒表示。DINからの番号も表示する。
// 始点を市長ボタン側、到着点を自転車側で目視する。逆ならconfig reverseを確認。test a / test bでJ4 / J5を照合。
// 両翼100%の後にtree_turns（両者合計のセンサー回転数）を溜め、ツリー頂点到達と同時にドレープ開始。
// 終演保持からsteadyで定常点灯。ceremonyは全準備を確認して次回の開演待機へ戻す。
// arm / arm shortで本披露/短い披露を待機。stopで試験や公演を終了し、全系統へ消灯を指令。
// 接続用Wi-Fi情報は起動時に表示。登録した会場PCからWebで予備開始や照明の個別操作ができる。
// 送信用スマホはIllumination-Controller接続後に /view か /send を前面で開くとSitesへ自動送信。公開版の手順はリポジトリ直下のREADME.md。
#include "src/OpeningShow/Addressable.h"
#include "src/OpeningShow/LocalControls.h"
#include "src/OpeningShow/OpeningGate.h"
#include "src/OpeningShow/Settings.h"
#include "src/OpeningShow/ShowRadio.h"
#include <Preferences.h>
#include <WebServer.h>
#include <esp_wifi_ap_get_sta_list.h>
#include "BikeWeb.h"
#include "DirectSite.h"
#include "PhoneSyncPage.h"

using namespace OpeningShow;
constexpr uint16_t TRAIL_CAPACITY = 100;
constexpr uint8_t FRAME_A = 19, FRAME_B = 21, PILOT = 22, LOAD_GATE = 23;
enum LightOutput : uint8_t { TrailOutput = UNIT_COUNT, FrameAOutput, FrameBOutput, PilotOutput, OutputCount };
enum TestOutput : uint8_t { TestTrail = 1, TestA = 2, TestB = 4, TestPilot = 8, TestAfter = 16, TestMark = 32 };
uint16_t trailMark = 0;
enum BikeSetting { TrailMs, WhiteMs, SparkleMs, ShortHoldMs, FadeMs, Pixels, RgbOrder,
                   Reverse, Brightness, Tail, Color, FrameA, FrameB, Pilot, TrailCurve, FrameRpmA, FrameRpmB,
                   TrailEffect, TrailAfter, TreeTurns, SteadyTrail };
Setting options[] = {
  {"trail_ms", TRAIL_MS, MIN_PHASE_MS, MAX_PHASE_MS}, {"white_ms", WHITE_MS, MIN_PHASE_MS, MAX_PHASE_MS},
  {"sparkle_ms", SPARKLE_MS, MIN_PHASE_MS, MAX_PHASE_MS}, {"short_hold_ms", SHORT_HOLD_MS, MIN_PHASE_MS, MAX_PHASE_MS},
  {"fade_ms", FADE_MS, MIN_PHASE_MS, MAX_PHASE_MS}, {"pixels", 50, 1, TRAIL_CAPACITY},
  {"rgb", 1, 0, 1}, {"reverse", 1, 0, 1}, {"brightness", 145, 0, 255},
  {"tail", 16, 1, 100}, {"color", 0x94ccff, 0, 0xffffff},
  {"frame_a", 192, 0, 255}, {"frame_b", 192, 0, 255}, {"pilot", 0, 0, 1},
  {"trail_curve", 1, 0, 1},
  {"frame_rpm_a", 90, 10, 500}, {"frame_rpm_b", 90, 10, 500},
  {"trail_fx", 1, 0, 2}, {"trail_after", 0, 0, 2}, {"tree_turns", 360, 1, 5000}, {"steady_trail", 1, 0, 1},
};
Settings config("tbt-bike-fx", options);

struct Peer {
  Packet last{};
  uint8_t mac[6]{};
  uint32_t seen = 0, conflictAt = 0;
  bool conflict = false;
};
Peer peers[UNIT_COUNT];
const char *const names[] = {"Bike", "Mayor", "Wing", "Tree lower", "Tree upper",
    "Drape A", "Drape B", "Plaza 1", "Plaza 2", "Plaza 3"};
const char *const phases[] = {"OFF", "ARMED", "TRAIL", "RIDE", "WHITE", "SPARKLE",
    "DRAPE", "HOLD", "SHORT HOLD", "FADE", "TREE CHARGE", "STEADY"};

Addressable<TRAIL_CAPACITY> strip(18, true, LOAD_GATE);
BoardLights lights(25, 26, 27, 32);
Contact button;
LineConsole<288> console;
ConsoleWatch watch;
Radio radio;
Sequence sequence;
Show::OpeningGate opening;
WebServer web(DirectSite::localAddress(), 80);
DirectSite directSite;
constexpr uint8_t OperatorSlots = 3;
constexpr char OperatorKey[] = "operator-macs";
uint8_t operatorMacs[OperatorSlots][6]{};

bool fault = false, stripOk = false, pwmA = false, pwmB = false, shortShow = false;
bool trailLit = false;
uint8_t test = 0, testEffect = 0, treeProgress = 0;
constexpr uint8_t SiteDemoOn = 12, SiteDemoOff = 13;
bool demoRunning = false;
uint32_t demoControl = 0, demoStarted = 0;
uint8_t progressA = 0, progressB = 0, plazaCount = 2, frameA = 0, frameB = 0;
uint16_t muted = 0;
uint32_t run = 0, lastCue = 0, lastFrame = 0, testAt = 0, token = 0, trailSettledAt = 0;
RideSample rideSample;
RideRate rideRate;
uint32_t rideSeen = 0;
uint32_t treeBoot = 0, treeStart[2]{};

Timings timings() {
  return {uint16_t(config[TrailMs]), uint16_t(config[WhiteMs]), uint16_t(config[SparkleMs]),
          uint16_t(config[ShortHoldMs]), uint16_t(config[FadeMs])};
}

bool fresh(const Peer &peer, uint32_t now) {
  return peer.last.boot && now - peer.seen < LINK_MS &&
      (!peer.conflict || now - peer.conflictAt >= LINK_MS);
}

bool ready(Unit unit, uint32_t now, bool sameRun) {
  const Peer &peer = peers[uint8_t(unit)];
  return fresh(peer, now) && (peer.last.flags & Ready) && !(peer.last.flags & Fault) &&
      (!sameRun || peer.last.run == run);
}

bool allReady(uint32_t now, bool sameRun, bool needMayor) {
  if (!ready(Unit::Wing, now, sameRun) || (needMayor && !ready(Unit::Mayor, now, sameRun))) return false;
  if (shortShow) return true;
  for (uint8_t unit = uint8_t(Unit::TreeLower); unit < uint8_t(Unit::Plaza1) + plazaCount; ++unit) {
    if (!ready(static_cast<Unit>(unit), now, sameRun)) return false;
  }
  return true;
}

void readReports(uint32_t now) {
  Received rx;
  while (radio.poll(rx)) {
    if (rx.tag == RIDE_SAMPLE_TAG) {
      const Peer &wing = peers[uint8_t(Unit::Wing)];
      if (ready(Unit::Wing, now, true) && !memcmp(wing.mac, rx.mac, sizeof wing.mac) &&
          wing.last.boot == rx.sample.boot && rx.sample.run == run) {
        rideSample = rx.sample;
        rideSeen = now;
      }
      continue;
    }
    if (rx.tag != TAG) continue;
    const Packet &p = rx.packet;
    if (p.kind != Kind::Report) continue;
    auto &peer = peers[uint8_t(p.unit)];
    const bool sameMac = !memcmp(peer.mac, rx.mac, sizeof(peer.mac));
    if (!sameMac && peer.last.boot && now - peer.seen < LINK_MS) {
      peer.conflict = true;
      peer.conflictAt = now;
      continue;
    }
    if (sameMac && p.boot == peer.last.boot && !newer(p.seq, peer.last.seq)) continue;
    memcpy(peer.mac, rx.mac, sizeof(peer.mac));
    peer.last = p;
    peer.seen = now;
  }
}

void sendCue(uint32_t now) {
  Packet cue;
  cue.run = run;
  cue.phase = sequence.phase();
  cue.elapsed = sequence.elapsed(now);
  cue.duration = timings().duration(cue.phase);
  cue.flags = (shortShow ? Short : 0) | (plazaCount == 3 ? ThreePlazas : 0);
  cue.muted = muted & PEER_LIGHTS;
  cue.a = progressA;
  cue.b = progressB;
  if (cue.phase == Phase::TreeCharge) cue.reserved = treeProgress;
  lastCue = now;
  radio.send(cue);
  if (cue.phase == Phase::Ride && rideRate.revision > rideSample.revision) {
    radio.broadcast(&rideRate, sizeof rideRate);
  }
}

bool haveRideSample(uint32_t now) {
  return rideSample.boot && rideSample.run == run &&
      rideSample.boot == peers[uint8_t(Unit::Wing)].last.boot && now - rideSeen < 750 &&
      ready(Unit::Wing, now, true);
}

void chargeTree(uint32_t now) {
  if (!haveRideSample(now)) return;
  if (!treeBoot) {
    // Earlier pulse totals may include part of the wing stage. Wait for a new sample.
    if (now - rideSeen >= sequence.elapsed(now)) return;
    treeBoot = rideSample.boot;
    treeStart[0] = rideSample.pulses[0];
    treeStart[1] = rideSample.pulses[1];
    return;
  }
  if (treeBoot != rideSample.boot) return;
  const uint32_t turnsA = rideSample.pulses[0] - treeStart[0];
  const uint32_t turnsB = rideSample.pulses[1] - treeStart[1];
  if (turnsA >= 0x80000000U || turnsB >= 0x80000000U) return;
  const uint64_t equivalent = uint64_t(turnsA) * rideSample.ppr[1] + uint64_t(turnsB) * rideSample.ppr[0];
  const uint64_t target = uint64_t(config[TreeTurns]) * rideSample.ppr[0] * rideSample.ppr[1];
  treeProgress = max(treeProgress, uint8_t(equivalent >= target ? 100 : equivalent * 100 / target));
}

constexpr uint8_t frameDuty(uint32_t cap, uint32_t rpm, uint32_t fullRpm) {
  const uint32_t floor = cap / 5;
  return floor + ((rpm < fullRpm ? rpm : fullRpm) * (cap - floor) + fullRpm / 2) / fullRpm;
}
static_assert(frameDuty(192, 0, 900) == 38 && frameDuty(192, 450, 900) == 115 &&
              frameDuty(192, 1800, 900) == 192);

bool applyRideRate(const String &command, uint32_t now) {
  if (sequence.phase() != Phase::Ride || fault || !haveRideSample(now)) return false;
  RideRate rate;
  if (!directSite.verifyRideRate(command, run, opening.generation, now, rate.revision, rate.value) ||
      rate.revision <= rideRate.revision) return false;
  rate.run = run;
  rate.boot = radio.boot();
  rideRate = rate;
  return true;
}

void enter(Phase phase, uint32_t now) {
  sequence.enter(phase, now);
  lastFrame = now - 30;
  Serial.printf("run=%lu phase=%s\n", (unsigned long)run, phases[uint8_t(phase)]);
  sendCue(now);
}

bool setFrame(uint8_t pin, uint8_t &previous, uint8_t value, bool &attached) {
  if (previous == value) return true;
  previous = value;
  if (attached && ledcWrite(pin, value)) return true;
  if (attached) ledcDetach(pin);
  attached = false;
  digitalWrite(pin, LOW);
  pinMode(pin, OUTPUT);
  return false;
}

bool bikeLights(uint8_t a, uint8_t b, bool pilot) {
  if (muted & (1U << FrameAOutput)) a = 0;
  if (muted & (1U << FrameBOutput)) b = 0;
  if (muted & (1U << PilotOutput)) pilot = false;
  const bool first = setFrame(FRAME_A, frameA, a, pwmA);
  const bool second = setFrame(FRAME_B, frameB, b, pwmB);
  digitalWrite(PILOT, pilot && first && second);
  // Q1 enables all three 12 V channels as well as the trail path via Q2.
  digitalWrite(LOAD_GATE, first && second && (a || b || pilot || trailLit));
  return first && second;
}

constexpr uint16_t trailHead(uint32_t age, uint16_t pixels, uint32_t duration) {
  const uint32_t travel = duration - 60;
  return age >= travel ? pixels - 1 : age * (pixels - 1) / travel;
}
static_assert(trailHead(0, 50, TRAIL_MS) == 0 && trailHead(TRAIL_MS - 30, 50, TRAIL_MS) == 49);
static_assert(trailHead(1, 1, MIN_PHASE_MS) == 0);

uint8_t trailIntensity(uint32_t brightness, uint32_t remaining, uint32_t tail) {
  return config[TrailCurve] ? brightness * remaining * remaining / (tail * tail) : brightness * remaining / tail;
}

constexpr uint16_t rgbSum(uint32_t color) {
  return (color >> 16) + ((color >> 8) & 255) + (color & 255);
}

bool drawTrail(uint32_t age, bool dim, uint8_t effect, bool settled = false, bool steady = false) {
  if ((!dim && (muted & (1U << TrailOutput))) || (steady && !config[SteadyTrail])) {
    const bool cleared = !trailLit || strip.clear();
    trailLit = false;
    return cleared;
  }
  const uint16_t count = config[Pixels], tail = config[Tail];
  const uint16_t head = trailHead(age, count, config[TrailMs]);
  const uint8_t brightness = dim ? min(config[Brightness], uint32_t{25}) : config[Brightness];
  strip.setRgbOrder(config[RgbOrder]);
  const auto colorAt = [&](uint16_t pixel) -> uint32_t {
    const int step = config[Reverse] ? count - 1 - pixel : pixel;
    if (settled) {
      const uint32_t cycle = (age / 14 + step * 13) % 512;
      const uint32_t wave = cycle < 256 ? cycle : 511 - cycle;
      if (steady) return scale(0xffb871, brightness * (60 + wave / 5) / 255);
      if (effect == 1) {
        const uint32_t tone = (age / 3600 + (step * 4 / count)) % 3;
        constexpr uint32_t colors[] = {0x8dcbff, 0xffbe72, 0xc4a6ff};
        // Crossfade to the next color over the full interval so there are no color jumps.
        const uint32_t mix = age % 3600;
        const uint32_t first = colors[tone], second = colors[(tone + 1) % 3];
        const uint32_t color = rgb(
            ((first >> 16) * (3600 - mix) + (second >> 16) * mix) / 3600,
            (((first >> 8) & 255) * (3600 - mix) + ((second >> 8) & 255) * mix) / 3600,
            ((first & 255) * (3600 - mix) + (second & 255) * mix) / 3600);
        return scale(color, brightness * (45 + wave / 2) / 255);
      }
      if (effect == 2) return scale(0xffcc8a, brightness * (40 + wave * wave / 520) / 255);
      const uint32_t seed = uint32_t(step * 73 + (age / 450) * 89) % 97;
      const uint32_t sparkle = seed < 13 ? (449 - age % 450) / 3 : 0;
      return scale(seed < 13 ? 0xffe3a3 : 0x94ccff, brightness * (48 + sparkle) / 255);
    }
    const int behind = head - step;
    if (behind < 0 || behind >= tail) return uint32_t{0};
    const uint32_t remaining = tail - behind;
    const uint32_t intensity = trailIntensity(brightness, remaining, tail);
    if (effect == 1) {
      const uint32_t seed = uint32_t(step * 73 + (age / 85) * 31) % 97;
      if (behind <= 1) return scale(0xe1f6ff, brightness);
      if (seed < 24) return scale(seed < 12 ? 0xffcc6e : 0xffffff, max(intensity, uint32_t(brightness * 3 / 5)));
      return scale(config[Color], intensity);
    }
    if (effect == 2) {
      const uint32_t color = behind <= tail / 3 ? 0xd5f2ff : 0xffb56a;
      return scale(color, brightness * remaining / tail);
    }
    return scale(config[Color], intensity);
  };
  uint32_t reference = 0, total = 0;
  for (uint16_t offset = 0; offset < min(count, tail); ++offset)
    reference += rgbSum(scale(config[Color], trailIntensity(brightness, tail - offset, tail)));
  uint32_t colors[TRAIL_CAPACITY];
  for (uint16_t pixel = 0; pixel < count; ++pixel) {
    colors[pixel] = colorAt(pixel);
    total += rgbSum(colors[pixel]);
  }
  const uint8_t level = total > reference ? reference * 255 / total : 255;
  const bool sent = strip.draw([&](uint16_t pixel) {
    return pixel < count ? scale(colors[pixel], level) : uint32_t{0};
  });
  trailLit = sent;
  return sent;
}

void clearShow(uint32_t now) {
  opening.disarm();
  muted = 0;
  test = 0;
  progressA = progressB = treeProgress = 0;
  treeBoot = 0;
  rideRate = RideRate{};
  if (stripOk && (trailLit || sequence.phase() == Phase::Trail)) fault |= !strip.clear();
  trailLit = false;
  fault |= !bikeLights(0, 0, false);
  run = esp_random() | 1U; // A distinct Off cue also stops local playback.
  enter(Phase::Off, now);
}

void stop(uint32_t now) {
  if (demoRunning) {
    demoRunning = false;
    demoControl = esp_random() | 1U;
  }
  clearShow(now);
}

bool arm(uint32_t now, bool isShort, bool needMayor) {
  if (fault || test || !radio.ready() || sequence.phase() != Phase::Off) return false;
  shortShow = isShort;
  if (!allReady(now, false, needMayor)) return false;
  run = esp_random() | 1U;
  opening.prepare(run);
  enter(Phase::Armed, now);
  return true;
}

bool start(uint32_t now, Show::OpeningSource via) {
  if (demoRunning || fault || sequence.phase() != Phase::Armed || !allReady(now, true, via == Show::MayorButton) ||
      !opening.reserve(run, via)) return false;
  opening.recordAcceptance(now);
  if (!drawTrail(0, false, config[TrailEffect])) {
    opening.recordOutput(Show::OutputFault, now);
    fault = true;
    stop(now);
    return false;
  }
  opening.recordOutput(Show::SentFrame, now);
  enter(Phase::Trail, now);
  return true;
}

bool drapeDone(Unit unit, uint32_t now) {
  return ready(unit, now, true) && (peers[uint8_t(unit)].last.flags & DrapeSent);
}

bool mayorPressed(uint32_t now) {
  return ready(Unit::Mayor, now, true) && (peers[uint8_t(Unit::Mayor)].last.flags & Pressed);
}

bool startFromSites(uint32_t now) {
  if (mayorPressed(now)) return start(now, Show::MayorButton);
  return start(now, Show::WebFallback);
}

void advance(Phase next, uint32_t now) {
  if (next == Phase::Off) { stop(now); return; }
  if (next == Phase::Ride) {
    trailSettledAt = now;
    const bool cleared = !shortShow || strip.clear();
    if (shortShow) trailLit = false;
    if (!cleared || !bikeLights(config[FrameA] / 5, config[FrameB] / 5, config[Pilot])) {
      fault = true;
      stop(now);
      return;
    }
  }
  if (sequence.phase() == Phase::TreeCharge && next == Phase::White) treeProgress = 100;
  enter(next, now);
}

void runShow(uint32_t now) {
  const Phase phase = sequence.phase();
  if (!demoRunning && phase == Phase::Armed && mayorPressed(now)) {
    start(now, Show::MayorButton);
    return;
  }
  if (phase == Phase::Ride && ready(Unit::Wing, now, true)) {
    const Packet &wing = peers[uint8_t(Unit::Wing)].last;
    progressA = max(progressA, wing.a);
    progressB = max(progressB, wing.b);
  }
  const bool goals = ready(Unit::Wing, now, true) && progressA == 100 && progressB == 100;
  const bool drapes = drapeDone(Unit::DrapeA, now) && drapeDone(Unit::DrapeB, now);
  if (phase == Phase::TreeCharge) chargeTree(now);
  const Phase next = sequence.next(now, shortShow, goals, drapes, timings(), treeProgress == 100);
  if (next != phase) {
    advance(next, now);
    return;
  }
  if (now - lastFrame < 30) return;
  lastFrame = now;
  if (phase == Phase::Trail || test) {
    const uint32_t age = test ? now - testAt : sequence.elapsed(now);
    const uint32_t testLimit = test & TestTrail ? config[TrailMs] :
        test == TestAfter ? 8500U : test == TestMark ? 4000U : test == TestPilot ? 1000U : 2000U;
    if (test && age >= testLimit) {
      if (test != TestMark) { stop(now); return; }
      test = 0;
      const bool cleared = strip.clear();
      trailLit = false;
      if (!bikeLights(0, 0, false) || !cleared) {
        fault = true;
        stop(now);
      }
      return;
    }
    const bool withTrail = phase == Phase::Trail || (test & (TestTrail | TestAfter));
    if (test == TestMark) {
      const auto white = scale(0xffffff, min(config[Brightness], uint32_t{25}));
      trailLit = strip.draw([&](uint16_t pixel) { return pixel == trailMark ? white : uint32_t{0}; });
    }
    if ((test == TestMark && !trailLit) ||
        (withTrail && !drawTrail(age, test != 0, test ? testEffect : config[TrailEffect], test == TestAfter)) ||
        !bikeLights(test & TestA ? min(config[FrameA], uint32_t{15}) : 0,
                    test & TestB ? min(config[FrameB], uint32_t{15}) : 0, test & TestPilot)) {
      fault = true;
      stop(now);
    }
  } else if (phase == Phase::Fade) {
    const uint32_t fade = config[FadeMs], left = fade - sequence.elapsed(now);
    if (!bikeLights(config[FrameA] * left / fade, config[FrameB] * left / fade, false)) {
      fault = true;
      stop(now);
    }
  } else if (!shortShow && isActive(phase) &&
      !drawTrail(now - trailSettledAt, false, config[TrailAfter], true, phase == Phase::Steady)) {
    fault = true;
    stop(now);
  } else if (phase == Phase::Ride || phase == Phase::TreeCharge) {
    const bool haveSample = haveRideSample(now);
    if (!bikeLights(frameDuty(config[FrameA], haveSample ? rideSample.rpm[0] : 0, config[FrameRpmA] * 10),
                    frameDuty(config[FrameB], haveSample ? rideSample.rpm[1] : 0, config[FrameRpmB] * 10), config[Pilot])) {
      fault = true;
      stop(now);
    }
  } else if (isActive(phase) &&
      !bikeLights(phase == Phase::Steady ? config[FrameA] / 4 : config[FrameA],
                  phase == Phase::Steady ? config[FrameB] / 4 : config[FrameB], phase != Phase::Steady && config[Pilot])) {
    fault = true;
    stop(now);
  }
}

uint8_t siteDemoAction(uint32_t now) {
  if (demoRunning) return SiteDemoOff;
  return sequence.phase() == Phase::Off && !fault && !test && radio.ready() &&
    allReady(now, false, false) ? SiteDemoOn : 0;
}

bool applySiteDemo(uint32_t now, uint8_t action) {
  if (!action || action != siteDemoAction(now)) return false;
  if (action == SiteDemoOff) { stop(now); return true; }
  demoControl = esp_random() | 1U;
  demoStarted = now;
  demoRunning = true;
  shortShow = false;
  clearShow(now);
  return true;
}

void tickSiteDemo(uint32_t now) {
  if (!demoRunning) return;
  if (fault || !allReady(now, false, false) || now - demoStarted >= 300000) { stop(now); return; }
  const Phase phase = sequence.phase();
  const uint32_t age = sequence.elapsed(now);
  if (phase == Phase::Off && age >= 1600) {
    if (!arm(now, false, false)) stop(now);
  } else if (phase == Phase::Armed && age >= 700) {
    if (!allReady(now, true, false)) stop(now);
    else enter(Phase::Steady, now);
  } else if (phase == Phase::Steady && age >= 2800) {
    clearShow(now);
  }
}

Phase siteAdvanceNext() {
  if (demoRunning || !opening.accepted || fault || test || !radio.ready()) return Phase::Off;
  const Phase next = sequence.backupNext(shortShow);
  return next == sequence.phase() || next == Phase::Fade ? Phase::Off : next;
}

bool applySiteStart(uint32_t now, Phase from, Phase next) {
  if (sequence.phase() != from) return false;
  if (from == Phase::Armed && next == Phase::Off) return startFromSites(now);
  if (next == Phase::Off || siteAdvanceNext() != next) return false;
  if (next == Phase::Armed) {
    if (!allReady(now, true, true)) return false;
    stop(now);
    if (!arm(now, false, true)) return false;
  } else {
    advance(next, now);
  }
  if (fault) return false;
  Serial.printf("Sites backup: %s -> %s\n", phases[uint8_t(from)], phases[uint8_t(next)]);
  return true;
}

void printProgress() {
  Serial.printf("%s A=%u%% B=%u%% tree=%u%% plazas=%u accepted=%u source=%u output=%u fault=%u test=%u J4/J5=%u/%u J6=%u\n",
      phases[uint8_t(sequence.phase())], progressA, progressB, treeProgress, plazaCount,
      opening.accepted, unsigned(opening.source), unsigned(opening.output), fault, test, frameA, frameB, digitalRead(PILOT));
}

void status(uint32_t now) {
  watch.printed(now);
  printProgress();
  Serial.printf("radio=%u local_button=%u profile=%s\n", radio.ready(), button.down(), shortShow ? "short" : "main");
  for (uint8_t i = 1; i < UNIT_COUNT; ++i) {
    const auto &p = peers[i];
    char age[11] = "-";
    if (p.last.boot) snprintf(age, sizeof(age), "%lu", (unsigned long)(now - p.seen));
    Serial.printf("%s link=%u age_ms=%s conflict=%u ready=%u run=%lu flags=%u\n", names[i], fresh(p, now), age,
        p.conflict && now - p.conflictAt < LINK_MS, ready(static_cast<Unit>(i), now, false),
        (unsigned long)p.last.run, p.last.flags);
  }
}

bool readStations(wifi_sta_mac_ip_list_t &stations) {
  wifi_sta_list_t connected{};
  return esp_wifi_ap_get_sta_list(&connected) == ESP_OK &&
      esp_wifi_ap_get_sta_list_with_ip(&connected, &stations) == ESP_OK;
}

bool stationAt(IPAddress ip, uint8_t (&mac)[6]) {
  wifi_sta_mac_ip_list_t stations{};
  if (!ip || !readStations(stations)) return false;
  for (int i = 0; i < stations.num; ++i) {
    if (stations.sta[i].ip.addr == uint32_t(ip)) {
      memcpy(mac, stations.sta[i].mac, sizeof mac);
      return true;
    }
  }
  return false;
}

unsigned operatorSlot(const uint8_t (&mac)[6]) {
  for (uint8_t i = 0; i < OperatorSlots; ++i) {
    if (memcmp(operatorMacs[i], mac, sizeof mac) == 0) return i + 1;
  }
  return 0;
}

bool operatorPC() {
  uint8_t mac[6]{};
  return stationAt(web.client().remoteIP(), mac) && operatorSlot(mac) != 0;
}

void configureOperator(const char *cmd, bool idle) {
  if (!strcmp(cmd, "list")) {
    wifi_sta_mac_ip_list_t stations{};
    if (!readStations(stations)) {
      Serial.println("操作PCの接続一覧を取得できません");
      return;
    }
    Serial.println("会場Wi-Fiに接続中の端末（IPv4と登録枠）");
    for (int i = 0; i < stations.num; ++i) {
      const auto &station = stations.sta[i];
      Serial.printf("%s slot=%u\n", IPAddress(station.ip.addr).toString().c_str(), operatorSlot(station.mac));
    }
    return;
  }
  if (!idle) {
    Serial.println("操作PCは停止・待機中に登録してください");
    return;
  }
  unsigned slot = 0;
  char value[16]{}, extra[2]{};
  if (sscanf(cmd, "%u %15s %1s", &slot, value, extra) != 2 || slot == 0 || slot > OperatorSlots) {
    Serial.println("operator 1|2|3 <IPv4またはclear>");
    return;
  }
  uint8_t mac[6]{};
  if (strcmp(value, "clear")) {
    IPAddress ip;
    if (!ip.fromString(value) || !stationAt(ip, mac)) {
      Serial.println("登録するPCを会場Wi-Fiにつなぎ、operator listでIPv4を確認してください");
      return;
    }
    const auto existing = operatorSlot(mac);
    if (existing && existing != slot) {
      Serial.printf("このPCは登録済みです slot=%u\n", existing);
      return;
    }
  }
  uint8_t updated[OperatorSlots][6];
  memcpy(updated, operatorMacs, sizeof updated);
  memcpy(updated[slot - 1], mac, sizeof mac);
  Preferences settings;
  if (settings.begin("tbt-bike", false)) {
    const bool saved = settings.putBytes(OperatorKey, updated, sizeof updated) == sizeof updated;
    settings.end();
    if (saved) {
      memcpy(operatorMacs, updated, sizeof operatorMacs);
      Serial.printf("操作PCの登録を更新しました slot=%u\n", slot);
      return;
    }
  }
  Serial.println("操作PCの登録を保存できません");
}

void help() {
  Serial.println("39 Bike / controller | 115200 bps, newline | help: command list");
  Serial.println("status: phase, outputs, link ages, duplicate IDs | stop: all lights off | plazas [2|3]");
  Serial.println("arm [local|short]: prepare show | start: start only after arm");
  Serial.println("OFF only: mark start (button) | mark end (bikes): J3, 4s; test a: J4 A; test b: J5 B; test pilot: J6 if enabled");
  Serial.println("test trail [0:comet 1:sparks 2:wave] | test after 0:twinkle 1:colors 2:warm wave | test a|b|pilot");
  Serial.println("steady (after show) | ceremony (from steady; arms only)");
  ConsoleWatch::help();
  Settings::help();
  Serial.println("operator list; operator 1|2|3 <IPv4またはclear>");
  Serial.println("site status | site staff: open one-time staff link while idle | site clear");
}

void command(const char *cmd, uint32_t now) {
  const bool idle = sequence.phase() == Phase::Off && !test && !demoRunning;
  if (watch.command(cmd, now) || config.command(cmd, idle)) return;
  if (!strcmp(cmd, "stop")) stop(now);
  else if (!strcmp(cmd, "status")) status(now);
  else if (!strcmp(cmd, "steady") || !strcmp(cmd, "ceremony")) {
    const bool steady = !strcmp(cmd, "steady");
    Serial.println(applySiteStart(now, steady ? Phase::Hold : Phase::Steady, steady ? Phase::Steady : Phase::Armed)
        ? "mode updated" : "mode refused; status");
  }
  else if (!strcmp(cmd, "start")) Serial.println(start(now, Show::SerialStart) ? "started" : "start refused");
  else if (!strcmp(cmd, "arm") || !strcmp(cmd, "arm local") || !strcmp(cmd, "arm short")) {
    Serial.println(!demoRunning && arm(now, !strcmp(cmd, "arm short"), strcmp(cmd, "arm local") != 0) ?
        "armed" : "arm refused; status");
  } else if (!strcmp(cmd, "mark start") || !strcmp(cmd, "mark end")) {
    if (!idle || fault) { Serial.println("ERR stop the show and clear faults before testing"); return; }
    const bool end = !strcmp(cmd, "mark end");
    trailMark = (end != bool(config[Reverse])) ? config[Pixels] - 1 : 0;
    strip.setRgbOrder(config[RgbOrder]);
    test = TestMark;
    testAt = now;
    Serial.printf("J3 %s: DIN pixel %u/%lu, 4s\n", end ? "end (bikes)" : "start (button)",
        unsigned(trailMark + 1), (unsigned long)config[Pixels]);
  } else if (!strcmp(cmd, "test") || !strncmp(cmd, "test ", 5)) {
    if (!idle || fault) { Serial.println("ERR stop the show and clear faults before testing"); return; }
    testEffect = config[TrailEffect];
    if (!strcmp(cmd, "test")) test = TestTrail | TestA | TestB;
    else if (!strcmp(cmd, "test trail")) test = TestTrail;
    else if ((!strncmp(cmd, "test trail ", 11) || !strncmp(cmd, "test after ", 11)) &&
             cmd[11] >= '0' && cmd[11] <= '2' && !cmd[12]) {
      test = cmd[5] == 'a' ? TestAfter : TestTrail;
      testEffect = cmd[11] - '0';
    }
    else if (!strcmp(cmd, "test a")) test = TestA;
    else if (!strcmp(cmd, "test b")) test = TestB;
    else if (!strcmp(cmd, "test pilot") && config[Pilot]) test = TestPilot;
    else if (!strcmp(cmd, "test pilot")) { Serial.println("ERR pilot disabled by config"); return; }
    else { Serial.println("Use test trail [0-2] | test after 0-2 | test a|b|pilot"); return; }
    testAt = now;
  } else if (!strcmp(cmd, "plazas")) {
    Serial.printf("plazas=%u\n", plazaCount);
  } else if ((!strcmp(cmd, "plazas 2") || !strcmp(cmd, "plazas 3")) && idle) {
    Preferences settings;
    if (settings.begin("tbt-bike", false)) {
      const uint8_t count = cmd[7] - '0';
      if (settings.putUChar("plazas", count) == 1) plazaCount = count;
      settings.end();
    }
    status(now);
  } else if (!strncmp(cmd, "operator ", 9)) {
    configureOperator(cmd + 9, idle);
  } else if (!directSite.command(cmd, idle)) help();
}

void webPage() {
  web.sendHeader("Cache-Control", "no-store");
  web.sendHeader("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; script-src 'self' 'unsafe-inline'; connect-src 'self' https://bicycle-illuminations-1128.feyhawthorn16.chatgpt.site; base-uri 'none'; frame-ancestors 'none'");
  web.send_P(200, "text/html; charset=utf-8", BIKE_WEB);
}

void operatorPage() {
  if (operatorPC()) webPage();
  else {
    web.sendHeader("Cache-Control", "no-store");
    web.sendHeader("Location", "/view");
    web.send(303);
  }
}

String statusJson(uint32_t now, bool operatorView) {
  const Phase phase = sequence.phase();
  String s;
  s.reserve(2100);
  s = "{\"phase\":\"";
  s += phases[uint8_t(phase)];
  s += String("\",\"run\":") + run + ",\"rev\":" + opening.generation + ",\"staffAuto\":1" +
       ",\"staffNext\":" + uint8_t(siteAdvanceNext()) +
       ",\"demoNext\":" + siteDemoAction(now) + ",\"demoControl\":" + demoControl + ",\"demoActive\":" + demoRunning +
       ",\"phaseId\":" + uint8_t(phase) + ",\"phaseElapsed\":" + sequence.elapsed(now) +
       ",\"phaseDuration\":" + timings().duration(phase) + ",\"muted\":" + muted +
       ",\"short\":" + shortShow + ",\"plazas\":" + plazaCount +
       ",\"a\":" + progressA + ",\"b\":" + progressB + ",\"tree\":" + treeProgress +
       ",\"treeTurns\":" + config[TreeTurns] + ",\"accepted\":" + opening.accepted +
       ",\"source\":" + uint8_t(opening.source) + ",\"output\":" + uint8_t(opening.output) +
       ",\"fallback\":" + uint8_t(opening.fallback) + ",\"remaining\":" + opening.remaining(now) +
       ",\"fault\":" + fault + ",\"test\":" + bool(test);
  if (operatorView) {
    s += String(",\"token\":") + token + ",\"next\":" + uint8_t(sequence.backupNext(shortShow));
  }
  if (haveRideSample(now)) {
    s += String(",\"ride\":{\"boot\":") + rideSample.boot + ",\"run\":" + run +
         ",\"revision\":" + rideSample.revision + ",\"lanes\":[";
    for (uint8_t i = 0; i < 2; ++i) {
      if (i) s += ',';
      s += String("[") + rideSample.pulses[i] + ',' + rideSample.rpm[i] + ',' + rideSample.ppr[i] +
           ',' + rideSample.goal[i] + ',' + rideSample.baseRate[i] + ',' + rideSample.applied[i] +
           ',' + (i ? frameB : frameA) + ']';
    }
    s += "]}";
  }
  s += ",\"peers\":[";
  for (uint8_t i = 1; i < UNIT_COUNT; ++i) {
    if (i > 1) s += ',';
    const Peer &peer = peers[i];
    const Unit unit = static_cast<Unit>(i);
    const bool link = fresh(peer, now);
    const bool following = link && run && peer.last.run == run;
    s += String("{\"id\":") + i + ",\"link\":" + link +
         ",\"conflict\":" + (peer.conflict && now - peer.conflictAt < LINK_MS) +
         ",\"following\":" + following + ",\"ready\":" + ready(unit, now, false) +
         ",\"roundTrip\":" + following +
         ",\"muted\":" + (following && (peer.last.muted & unitLight(unit))) +
         ",\"fault\":" + (link && (peer.last.flags & Fault)) +
         ",\"pressed\":" + (unit == Unit::Mayor && following && (peer.last.flags & Pressed)) +
         ",\"done\":" + (isDrape(unit) && drapeDone(unit, now)) +
         ",\"cue\":" + (following ? uint8_t(peer.last.phase) : 0) + "}";
  }
  s += "]}";
  return s;
}

void webStatus(bool operatorView) {
  if (operatorView && !operatorPC()) { web.send(403); return; }
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", statusJson(millis(), operatorView));
}

uint32_t arg(const char *name) { return strtoul(web.arg(name).c_str(), nullptr, 10); }

void webCommand() {
  if (!operatorPC() || arg("token") != token) { web.send(403); return; }
  const String action = web.arg("action");
  const uint32_t id = arg("run"), rev = arg("rev"), now = millis();
  const uint32_t attempt = arg("attempt"), step = arg("step");
  const uint32_t page = arg("page");
  bool ok = false;
  if (action == "arm" || action == "short" || action == "local") {
    ok = !demoRunning && id == run && arm(now, action == "short", action != "local");
  } else if (action == "confirm") ok = opening.confirm(id, rev, now);
  else if (action == "cancel") ok = opening.cancelConfirmation(id, rev);
  else if ((action == "advance" || action == "light-stop" || action == "light-resume") &&
           id == run && web.hasArg("phase") && arg("phase") == uint8_t(sequence.phase()) &&
           isActive(sequence.phase()) && !fault) {
    if (action == "advance") {
      const Phase next = sequence.backupNext(shortShow);
      if (next == Phase::Fade) { advance(next, now); ok = !fault; }
      else if (next != sequence.phase()) ok = applySiteStart(now, sequence.phase(), next);
    } else {
      const uint32_t light = arg("light");
      if (light >= uint8_t(Unit::Wing) && light < OutputCount &&
          (light != uint8_t(Unit::Plaza3) || plazaCount == 3) &&
          (light >= UNIT_COUNT || fresh(peers[light], now))) {
        const uint16_t mask = 1U << light;
        if (action == "light-stop") muted |= mask;
        else muted &= ~mask;
        sendCue(now);
        ok = true;
      }
    }
  }
  else if (page && opening.matchesConfirmation(id, rev)) {
    opening.tick(now, opening.holdPage);
    if (action == "hold" && !opening.holding) {
      ok = opening.beginHold(id, page, attempt, now);
    } else if (page == opening.holdPage && attempt == opening.holdId) {
      if (action == "step") ok = opening.trackHold(id, page, attempt, step, now);
      else if (action == "release") {
        if (mayorPressed(now)) start(now, Show::MayorButton);
        ok = opening.finishHold(id, page, attempt, step, now) && start(now, Show::WebFallback);
      } else if (action == "abort") { opening.cancelHold(id, page, attempt); ok = true; }
    }
  }
  web.send(ok ? 200 : 409, "text/plain", ok ? "OK" : "Unavailable");
}

void setup() {
  Serial.setRxBufferSize(512);
  Serial.begin(115200);
  config.begin();
  lights.begin();
  button.begin(34);
  for (uint8_t pin : {FRAME_A, FRAME_B, PILOT}) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }
  pwmA = ledcAttach(FRAME_A, 1000, 8) && ledcWrite(FRAME_A, 0);
  pwmB = ledcAttach(FRAME_B, 1000, 8) && ledcWrite(FRAME_B, 0);
  stripOk = strip.begin();
  fault = !stripOk || !pwmA || !pwmB;
  Preferences settings;
  if (settings.begin("tbt-bike", true)) {
    plazaCount = settings.getUChar("plazas", 2) == 3 ? 3 : 2;
    if (settings.getBytesLength(OperatorKey) == sizeof operatorMacs) {
      settings.getBytes(OperatorKey, operatorMacs, sizeof operatorMacs);
    }
    settings.end();
  }
  char password[9];
  snprintf(password, sizeof(password), "%08lx", (unsigned long)esp_random());
  token = esp_random() | 1U;
  demoControl = esp_random() | 1U;
  run = esp_random() | 1U; // Match replies to this boot even in the initial Off phase.
  if (radio.begin("Illumination-Controller", password)) {
    directSite.begin(CHANNEL);
    web.on("/", HTTP_GET, operatorPage);
    web.on("/view", HTTP_GET, webPage);
    web.on("/status", HTTP_GET, [] { webStatus(false); });
    web.on("/phone-sync", HTTP_GET, [] { directSite.phoneResponse(web, millis()); });
    web.on("/staff-start", HTTP_POST, [] {
      const uint32_t now = millis();
      const String command = web.arg("plain");
      const uint8_t demo = siteDemoAction(now);
      if (demo && directSite.verifyStart(command, demoControl, 1, now, 0, demo)) {
        web.send(applySiteDemo(now, demo) ? 204 : 409);
        return;
      }
      const Phase from = sequence.phase(), next = siteAdvanceNext();
      bool started = false;
      if ((from == Phase::Armed || next != Phase::Off) &&
          directSite.verifyStart(command, run, opening.generation, now, uint8_t(from), uint8_t(next))) {
        if (from != Phase::Armed) {
          readReports(now);
          if (!fault) runShow(now);
        }
        started = applySiteStart(now, from, next);
      }
      web.send(started ? 204 : 409);
    });
    web.on("/ride-rate", HTTP_POST, [] {
      web.send(applyRideRate(web.arg("plain"), millis()) ? 204 : 409);
    });
    web.on("/phone-sync.js", HTTP_GET, [] {
      web.sendHeader("Cache-Control", "no-store");
      web.send_P(200, "text/javascript; charset=utf-8", PHONE_SYNC_SCRIPT);
    });
    web.on("/send", HTTP_GET, [] {
      web.sendHeader("Cache-Control", "no-store");
      web.sendHeader("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; script-src 'self'; connect-src 'self' https://bicycle-illuminations-1128.feyhawthorn16.chatgpt.site; base-uri 'none'; frame-ancestors 'none'");
      web.send_P(200, "text/html; charset=utf-8", PHONE_SYNC_PAGE);
    });
    web.on("/operator-status", HTTP_GET, [] { webStatus(true); });
    web.on("/action", HTTP_POST, webCommand);
    web.begin();
    Serial.printf("Wi-Fi Illumination-Controller password=%s http://192.168.4.1/\n", password);
  }
  help();
}

void loop() {
  uint32_t now = millis();
  readReports(now);
  web.handleClient();
  now = millis();
  if (const char *cmd = console.read()) command(cmd, now);
  if (button.pressed(now)) {
    if (demoRunning) stop(now);
    else if (sequence.phase() == Phase::Armed) start(now, Show::LocalTact);
    else if (isActive(sequence.phase()) || test) stop(now);
  }
  opening.tick(now, opening.holdPage);
  tickSiteDemo(now);
  if (!fault) runShow(now);
  const Phase from = sequence.phase(), next = siteAdvanceNext();
  const uint8_t demo = siteDemoAction(now);
  if (demo && directSite.takeStart(demoControl, 1, now, 0, demo)) {
    applySiteDemo(now, demo);
  } else if (!demo && directSite.takeStart(run, opening.generation, now, uint8_t(from), uint8_t(next))) {
    applySiteStart(now, from, next);
  }
  if (const String rate = directSite.takeRideRate(); rate.length()) applyRideRate(rate, now);
  if (now - lastCue >= RADIO_MS) sendCue(now);
  lights.update(!fault, allReady(now, isActive(sequence.phase()) || sequence.phase() == Phase::Armed, false),
                isActive(sequence.phase()) || test, fault || !radio.ready());
  if (directSite.sampleDue(now)) directSite.publish(statusJson(now, false), now);
  if (watch.due(now)) { watch.printed(now); printProgress(); }
  delay(2);
}

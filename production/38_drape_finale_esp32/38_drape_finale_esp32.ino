// ESP32-DevKitC V4（WROOM-32E）、Bike-Drape Rev B2。1台につき10 m・100灯。
// J3-1: DIN（GPIO18から5 V変換）。J7-1/2: LED用5 V/GND。J2: LED用5 V入力、J8: 制御5 V。
// 115200 bps、改行あり。id 1=A、id 2=B。ツリーの白開始で、2箱が中央から計4方向へ火花を走らせる。
// 最終到達のフレームを送信してから完了を返す。Steadyでは暗めの金色で固定する。
// help: コマンド一覧。status: A/Bの箱番号・進行段階・通信・点灯指令・最終フレーム送信・異常。
// watch 1000: 状態を1秒ごとに表示、watch offで停止。idで箱番号のみ確認。
// config / config pixels / config export: 設定一覧・単項目・復元用set行。変更は停止中に行う。
// 待機中はpで現地再生、p loopで繰り返し、test rgbで色順、test pixel 10でDINから10番目を低輝度確認。
// mark start / middle / end（例: mark middle）でDIN先頭／演出の中央／末端を白で3秒表示。中央が吊り位置に来ることを確認。
// set afterglow 1: 金色のきらきら（初期値）、2: 色ごとに滑らかに明暗、3: 穏やかな金色の呼吸。
// testで流れから到達まで、test holdで到達後の設定だけを約9秒、低輝度で目視する。
// LOCAL STARTで現地再生/消灯。シリアルのstopまたは0で消灯。実際の発光は目視で確認する。
#include "src/OpeningShow/Addressable.h"
#include "src/OpeningShow/LocalControls.h"
#include "src/OpeningShow/ShowNode.h"
#include "src/OpeningShow/Settings.h"
#include "src/BicycleLighting/DrapeFinale.h"

using namespace OpeningShow;
constexpr uint16_t CAPACITY = 100;
constexpr uint32_t HOLD_TEST_MS = 9000;
constexpr uint32_t GOLD = swapRedGreen(DrapeShow::GOLD_GRB);
enum AfterglowLook { GoldSparkle = 1, ColorBreath, QuietGold };
enum DrapeSetting { Pixels, RgbOrder, Brightness, SweepMs, Sparks, LoopHoldMs, Afterglow };
Setting options[] = {
  {"pixels", 100, 1, CAPACITY}, {"rgb", 1, 0, 1}, {"brightness", 255, 8, 255},
  {"sweep_ms", DrapeShow::SWEEP_MS, 500, 30000}, {"sparks", 255, 0, 255},
  {"loop_hold_ms", 800, 100, 10000}, {"afterglow", GoldSparkle, GoldSparkle, QuietGold},
};
Settings config("tbt-drape-fx", options);

Addressable<CAPACITY> strip(18, true, 23);
BoardLights lights(25, 26, 27, 32);
Contact button;
Console console;
ConsoleWatch watch;
ShowNode node;
enum class Playback : uint8_t { Off, Once, Repeat, Test, HoldTest, Colors, Pixel };
Playback playback = Playback::Off;
uint16_t testPixel = 0, testLastPixel = 0;
bool fault = false, lit = false, done = false, steadyLit = false;
uint32_t lastFrame = 0, manualAt = 0;
uint32_t remoteAt = 0, remoteRun = 0, remoteBoot = 0;

void stop(bool suppress) {
  if (suppress) node.suppress();
  playback = Playback::Off;
  done = false;
  if (lit) fault |= !strip.clear();
  lit = false;
  steadyLit = false;
}

void play(uint32_t now, Playback mode) {
  playback = mode;
  manualAt = now;
  done = false;
  lastFrame = now - DrapeFinale::FRAME_MS;
}

uint8_t smoothPulse(uint32_t elapsed, uint16_t period) {
  const uint16_t phase = elapsed % period;
  const uint32_t ramp = 510U * (phase <= period / 2 ? phase : period - phase) / period;
  return ramp * ramp * (765U - 2 * ramp) / 65025U;
}

uint32_t holdColor(uint16_t pixel, uint16_t count, uint32_t elapsed, uint32_t look, uint32_t sparks) {
  uint32_t color;
  const uint16_t distance = DrapeShow::distanceFromTop(pixel, count);
  if (look == GoldSparkle) {
    const uint16_t phase = (elapsed + distance * 719U) % 2600U;
    const uint8_t shimmer = phase < 340 ? smoothPulse(phase, 340) : 0;
    color = scale(GOLD, 170 + shimmer * sparks * 85U / (255U * 255U));
  } else if (look == ColorBreath) {
    // Warm colors with a combined RGB value no higher than the original full gold frame.
    constexpr uint32_t colors[] = {0x3c3b12, 0x522411, 0x541b0c};
    const uint8_t band = (distance / 6) % 3;
    color = scale(colors[band], 150 + smoothPulse(elapsed + band * 1600U, 4800) * 105U / 255U);
  } else {
    color = scale(GOLD, 135 + smoothPulse(elapsed, 8000) * 50U / 255U);
  }
  const uint8_t blend = elapsed < 700 ? elapsed * 255U / 700U : 255;
  return scale(GOLD, 255 - blend) + scale(color, blend);
}

void drawFrame(uint32_t elapsed, uint32_t sweep, bool steady) {
  const uint16_t count = config[Pixels];
  const bool colors = playback == Playback::Colors;
  const bool pixelOnly = playback == Playback::Pixel;
  const bool holdTest = playback == Playback::HoldTest;
  const bool dim = steady || playback == Playback::Test || holdTest || colors || pixelOnly;
  const uint8_t brightness = dim ? min(config[Brightness], uint32_t{30}) : config[Brightness];
  const uint32_t effectTime = min(elapsed, sweep) * DrapeShow::SWEEP_MS / sweep;
  auto frame = DrapeFinale::frameAt(effectTime, count);
  frame.sparkStrength = frame.sparkStrength * config[Sparks] / 255;
  strip.setRgbOrder(config[RgbOrder]);
  fault = !strip.draw([&](uint16_t pixel) {
    if (pixel >= count) return uint32_t{0};
    if (steady) return scale(GOLD, brightness);
    if (pixelOnly) return pixel >= testPixel && pixel <= testLastPixel ? scale(0xffffff, brightness) : uint32_t{0};
    if (colors) return scale(0xff0000UL >> (8 * (elapsed / 1000)), brightness);
    const uint32_t color = done || holdTest
        ? holdColor(pixel, count, holdTest ? elapsed : elapsed - sweep, config[Afterglow], config[Sparks])
        : swapRedGreen(DrapeFinale::pixelGrb(pixel, pixel, frame, count));
    return scale(color, brightness);
  });
  lit = !fault;
  steadyLit = steady && lit;
  if (fault) strip.clear();
  if (!done && lit && !steady && !colors && !pixelOnly && !holdTest && elapsed >= sweep) {
    done = true;
    Serial.println("Final frame sent");
  }
}

void help() {
  Serial.println("38 Drape | 115200 bps, newline | help: command list");
  Serial.println("status: ID, link, phase and output | id [1=A|2=B] | p [loop]: play | stop / 0: blackout");
  Serial.println("OFF: mark start / mark middle / mark end: 3s at DIN / hanging center / tail");
  Serial.println("test: dim show | test hold: afterglow | test rgb: color order | test pixel N: only DIN position N, 3s");
  Serial.println("set afterglow 1=gold sparkle / 2=color breath / 3=quiet gold");
  ConsoleWatch::help();
  Settings::help();
}

void status(uint32_t now) {
  Serial.printf("38 drape id=%u (%s) pixels=%lu phase=%s link=%u radio=%u manual=%u out=%u frame_sent=%u fault=%u\n",
      node.localId(), node.localId() == 1 ? "A" : node.localId() == 2 ? "B" : "unassigned",
      (unsigned long)config[Pixels], phaseLabel(node.phase(now)), node.follow.linked(now), node.radio.ready(),
      playback != Playback::Off, lit, done, fault);
}

void setup() {
  Serial.begin(115200);
  config.begin();
  lights.begin();
  button.begin(34);
  fault = !strip.begin();
  node.begin(Unit::DrapeA, Unit::DrapeB);
  help();
  status(millis());
}

void loop() {
  const uint32_t now = millis();
  if (node.receive(now)) stop(false);
  const Phase before = node.phase(now);
  bool showStatus = false;
  if (const char *cmd = console.read()) {
    if (watch.command(cmd, now) || config.command(cmd, node.idle(now) && playback == Playback::Off)) {
      // Settings commands do not start output.
    } else if (!strcmp(cmd, "id")) {
      Serial.printf("id=%u (0=unassigned, 1=A, 2=B)\n", node.localId());
    } else if (strncmp(cmd, "id ", 3) == 0 && strlen(cmd) == 4 && playback == Playback::Off) {
      Serial.println(node.select(cmd[3] - '0') ? "id saved" : "id refused");
    } else if (!strcmp(cmd, "status")) {
      showStatus = true;
    } else if (!strcmp(cmd, "help")) {
      help();
    } else if (!strcmp(cmd, "stop") || !strcmp(cmd, "0")) {
      stop(true);
    } else if (!fault && node.idle(now)) {
      if (!strcmp(cmd, "p")) play(now, Playback::Once);
      else if (!strcmp(cmd, "p loop")) play(now, Playback::Repeat);
      else if (!strcmp(cmd, "test")) play(now, Playback::Test);
      else if (!strcmp(cmd, "test hold")) play(now, Playback::HoldTest);
      else if (!strcmp(cmd, "test rgb")) play(now, Playback::Colors);
      else if (!strncmp(cmd, "mark ", 5) || !strncmp(cmd, "test pixel ", 11)) {
        const uint16_t count = config[Pixels];
        uint32_t first = 1, last = 1;
        if (!strcmp(cmd, "mark middle")) { first = (count + 1) / 2; last = count / 2 + 1; }
        else if (!strcmp(cmd, "mark end")) first = last = count;
        else if (!strncmp(cmd, "test pixel ", 11) && Settings::number(cmd + 11, 1, count, first)) last = first;
        else if (strcmp(cmd, "mark start")) last = 0;
        if (last) {
          testPixel = first - 1;
          testLastPixel = last - 1;
          play(now, Playback::Pixel);
          Serial.printf("38 id=%u DIN pixels %lu..%lu/%u, 3s\n", node.localId(),
              (unsigned long)first, (unsigned long)last, unsigned(count));
        } else {
          Serial.printf("ERR pixel range 1..%u; use mark start|middle|end or test pixel N\n", unsigned(count));
        }
      }
      else help();
    } else help();
  }
  if (button.pressed(now)) {
    if (playback != Playback::Off || isActive(before)) stop(true);
    else if (node.idle(now) && !fault) play(now, Playback::Once);
  }
  const Phase phase = node.phase(now);
  const uint32_t sweep = config[SweepMs];
  const uint32_t localAge = now - manualAt;
  if (playback != Playback::Off &&
      (!node.idle(now) || (playback == Playback::Test && localAge >= sweep + 1300) ||
       (playback == Playback::HoldTest && localAge >= HOLD_TEST_MS) ||
       ((playback == Playback::Colors || playback == Playback::Pixel) && localAge >= 3000))) stop(false);
  if (playback == Playback::Repeat && done && localAge >= sweep + config[LoopHoldMs])
    play(now, Playback::Repeat);

  const bool finale = phase == Phase::White || phase == Phase::Sparkle || phase == Phase::Drape || phase == Phase::Hold;
  if (finale) {
    const auto &cue = node.follow.packet();
    if (remoteRun != cue.run || remoteBoot != cue.boot) {
      // Keep the White start across later phases and individual mute/unmute.
      remoteAt = now - node.follow.elapsed(now);
      remoteRun = cue.run;
      remoteBoot = cue.boot;
      done = false;
      lastFrame = now - DrapeFinale::FRAME_MS;
    }
  } else remoteRun = 0;

  const bool manual = playback != Playback::Off;
  const bool steady = phase == Phase::Steady;
  const bool remote = !node.muted(now) && (finale || steady);
  if ((manual || remote) && !fault) {
    if ((!steady || !steadyLit) && now - lastFrame >= DrapeFinale::FRAME_MS) {
      lastFrame = now;
      drawFrame(manual ? now - manualAt : finale ? now - remoteAt : 0, sweep, steady);
    }
  } else if (lit || done) {
    stop(false);
  }
  node.report(now, fault ? Fault : manual ? 0 : Ready | (remote && finale && done ? DrapeSent : 0));
  lights.update(!fault, node.follow.linked(now), lit, fault || !node.radio.ready());
  if (showStatus || watch.due(now)) { watch.printed(now); status(now); }
  delay(2);
}

// ESP32 Dev Module。無電圧のNOをGPIO18、COMをGNDに接続する新規配線案。
// 手動予備タクトはGPIO19とGND、状態LEDはGPIO25から直列抵抗を経由してGND。
// 照光用の電源線は接点入力から分離。配線済みの箱は導通でGPIOを確認してから設定を合わせる。
// 115200 bps、改行あり。helpで一覧。statusで市長/予備接点、解放、送信待ち、受信段階、状態LED。
// watch 500で接点を繰り返し表示、watch offで停止。config / config debounce_ms / config exportで保存値確認。
// testは停止中に状態LEDを2秒点灯。stopはこの試験を終了。set debounce_ms 35等は停止中に行う。
// test contactは39番がOFFの間、10秒間押した・離したを表示する。statusのmayor/backupは1=押下。
// 本番は39番で開演準備し、解放後に一度押す。受理・軌跡出力は39番と現物で確認する。
#include "src/OpeningShow/LocalControls.h"
#include "src/OpeningShow/ShowNode.h"
#include "src/OpeningShow/OpeningGate.h"
#include "src/OpeningShow/Settings.h"

using namespace OpeningShow;
constexpr uint8_t MAYOR_PIN = 18;
constexpr uint8_t BACKUP_PIN = 19;
constexpr uint8_t STATUS_PIN = 25;
enum MayorSetting { DebounceMs, Backup };
Setting options[] = {{"debounce_ms", 35, 20, 120}, {"backup", 1, 0, 1}};
Settings config("tbt-mayor", options);
ShowNode node;
Console console;
ConsoleWatch watch;
Show::MayorContact press;
bool pending = false, test = false;
bool contactTest = false, lastMayorDown = false, lastBackupDown = false;
uint32_t invitation = 0, testAt = 0;
uint32_t contactTestAt = 0;

void help() {
  Serial.println("42 Mayor button | 115200 bps, newline | status: contacts, accepted release, pending, radio, LED");
  Serial.println("mayor=GPIO18, backup=GPIO19: 1=pressed (LOW), 0=released | LED=GPIO25");
  Serial.println("OFF only: test = status LED 2s | test contact = log raw press/release for 10s | stop: end tests");
  ConsoleWatch::help();
  Settings::help();
}

void setup() {
  Serial.begin(115200);
  config.begin();
  pinMode(MAYOR_PIN, INPUT_PULLUP);
  pinMode(BACKUP_PIN, INPUT_PULLUP);
  digitalWrite(STATUS_PIN, LOW);
  pinMode(STATUS_PIN, OUTPUT);
  node.begin(Unit::Mayor, Unit::Mayor);
  Serial.println(node.radio.ready() ? "Mayor ready" : "Radio failed");
  help();
}

void loop() {
  const uint32_t now = millis();
  node.receive(now);
  const Phase phase = node.phase(now);
  const uint32_t run = phase == Phase::Armed ? node.follow.packet().run : 0;
  if (run != invitation) { invitation = run; pending = false; }
  const bool mayorDown = digitalRead(MAYOR_PIN) == LOW;
  const bool backupDown = digitalRead(BACKUP_PIN) == LOW;
  const bool down = mayorDown || (config[Backup] && backupDown);
  const bool idle = node.idle(now);
  bool showStatus = false;
  if (const char *cmd = console.read()) {
    if (!watch.command(cmd, now) && !config.command(cmd, idle && !test)) {
      if (!strcmp(cmd, "status")) showStatus = true;
      else if (!strcmp(cmd, "stop")) test = contactTest = false;
      else if (idle && !strcmp(cmd, "test")) { test = true; testAt = now; }
      else if (idle && !strcmp(cmd, "test contact")) {
        contactTest = true;
        contactTestAt = now;
        lastMayorDown = mayorDown;
        lastBackupDown = backupDown;
        Serial.println("Contact test started for 10s. Press and release each button.");
        showStatus = true;
      }
      else help();
    }
  }
  if (!idle || (test && now - testAt >= 2000)) test = false;
  if (contactTest) {
    if (!idle || now - contactTestAt >= 10000) {
      contactTest = false;
      Serial.println("Contact test ended");
    } else {
      if (mayorDown != lastMayorDown) Serial.printf("mayor %s\n", mayorDown ? "pressed" : "released");
      if (backupDown != lastBackupDown) Serial.printf("backup %s\n", backupDown ? "pressed" : "released");
      lastMayorDown = mayorDown;
      lastBackupDown = backupDown;
    }
  }
  if (press.poll(run, down, now, config[DebounceMs])) {
    pending = true;
    Serial.println("Contact pressed; waiting for Bike");
  }
  const bool available = !test && !contactTest && (!down || pending);
  node.report(now, (available ? Ready : 0) | (pending ? Pressed : 0));
  const bool lamp = test || (phase == Phase::Armed ? (pending || press.released) : isActive(phase));
  digitalWrite(STATUS_PIN, lamp);
  if (showStatus || watch.due(now)) {
    watch.printed(now);
    Serial.printf("phase=%s link=%u mayor=%u backup=%u backup_enabled=%u pending=%u released=%u radio=%u LED=%u\n",
        phaseLabel(phase), node.follow.linked(now), mayorDown, backupDown, bool(config[Backup]),
        pending, press.released, node.radio.ready(), lamp);
  }
  delay(2);
}

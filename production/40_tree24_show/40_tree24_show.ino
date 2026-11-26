// Tree24 A3。J5は制御5 V、J24は照明24 V。T1〜T4は1:24V、2:金、3:白。
// ESP32 Dev Module、115200 bps、改行あり。id 1で下4本、id 2で上3本を保存。
// help: 操作一覧。status: 箱番号・使用本数・進行・通信・異常とT1〜T4の金/白PWM指令値。
// watch 1000で定期表示、watch offで停止。idで箱番号、config / config step_pct / config exportで設定確認。
// testで全枝、test 1でT1の金→白を低輝度で試す。4秒で消灯。発光は現物を目視する。
// mapで各T端子が下から何段目かを表示。reverse設定も反映。接続はtest 1等で1本ずつ確認する。
// 39番の停止中はMANUALまたはpで金、p sweepで白の走査を確認。再押下またはstopで消灯。
#include "src/OpeningShow/Tree24.h"

OpeningShow::Tree24 tree(false);
void setup() { tree.begin(); }
void loop() { tree.tick(); }

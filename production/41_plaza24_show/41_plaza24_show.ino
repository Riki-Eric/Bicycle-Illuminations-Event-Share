// Tree24 A3。J5は制御5 V、J24は照明24 V。T1〜T4は1:24V、2:金、3:白。
// ESP32 Dev Module、115200 bps、改行あり。各箱に異なるid 1〜3を割り当てる。
// help: 操作一覧。status: 箱番号・使用本数・進行・通信・異常とT1〜T4の金/白PWM指令値。
// watch 1000で定期表示、watch offで停止。id / branchesで割当、config / config exportで設定確認。
// 39番の停止中にbranches 1..4、set gold 83等を保存。test 1でT1の金→白を低輝度で試す。
// mapで箱のIDと使用端子を表示。test 1等で光った外周の場所と箱・端子名を現地で記録する。
// 本番は39番の最終点灯指令で点灯。待機中はMANUALまたはpで試し、再押下またはstopで消灯。
#include "src/OpeningShow/Tree24.h"

OpeningShow::Tree24 plaza(true);
void setup() { plaza.begin(); }
void loop() { plaza.tick(); }

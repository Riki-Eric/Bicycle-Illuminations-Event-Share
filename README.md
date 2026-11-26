# 自転車イルミネーション　点灯式の共有資料

Arduino IDEで使う現行の本番プログラム、単体試験、基板の配線図です。[部品面・はんだ面と回路図](wiring/README.md)

## 書き込み

1. GitHubの **Code → Download ZIP** で取得したら、すべて展開します。使うスケッチのフォルダー内にある、フォルダー名と同じ .ino をArduino IDEで開きます。
2. Arduino IDEのボードマネージャーで対象のコアを入れます。DevKitC V4は **esp32 3.3.12 / ESP32 Dev Module**、翼予備機のXIAOは **esp32 3.3.12 / XIAO_ESP32C3 / USB CDC On Boot → Enabled**、classic Nanoは **Arduino AVR Boards 1.8.8 / Arduino Nano / ATmega328P** です。Nanoのブートローダーが旧版なら、書き込み設定だけを ATmega328P (Old Bootloader) に合わせます。
3. 各機器の負荷電源を切って書き込みます。必要なソースとライブラリはスケッチ内の src/ に同梱しています。ESP32のWi‑Fi・HTTP・RMTなどはESP32コアに付属します。NeoPixelのライセンスは同梱しています。
4. 現地でUSBシリアルを **115200 bps・改行あり**にし、help と status がある機器は表示を確認します。各基板の設定とコマンドはスケッチの冒頭にもあります。

## 本番用

| 番号・機器 | 設営時の確認 |
| --- | --- |
| [27　回転表示](production/27_rider_bars_receiver/) | 車体Aを role a、Bを role b。test 1、test 8、test で緑側から赤側の並びを確認。p で回転表示へ。 |
| [38　ドレープ](production/38_drape_finale_esp32/) | Aは id 1、Bは id 2。mark start、mark middle、mark end でテープの始端・中央・末端を確認。 |
| [39　進行・軌跡・車体](production/39_bike_opening_show/) | mark start、mark end で軌跡の両端、test a・test b でJ4・J5を確認。開演前に status。本番は arm、全系統の消灯は stop。 |
| [40　ツリー](production/40_tree24_show/) | id 1 は下4段、id 2 は上3段。map と test 1 などで担当段と金・白を確認。 |
| [41　広場外周](production/41_plaza24_show/) | 箱ごとに異なる id 1〜3。branches で担当本数を登録し、map と test 1 などで点灯位置を確認。3箱なら39番で plazas 3。 |
| [42　市長ボタン台](production/42_mayor_button/) | **XIAOで使うGPIOは現物照合待ち。** 照合後、test contact で押し・離し、test で状態LEDを確認。 |
| [43　翼A](production/43_wing_opening_show/) | map と mark a root、mark a tip、mark b root、mark b tip で向きを確認。左右の円板を片側ずつ回し、count で各1周が6パルスになることを確認。 |
| [44　翼予備機のNano](production/44_wing_arduino_bridge/) | D8／A0を43番とそろえる。status で受信数とエラー数を確認し、43番の目印で実際の点灯を確認。 |

38〜43番はこの版を組み合わせます。単体試験は39番がOFFで行い、箱・端子・実際に光った場所を照合します。回転数は円板軸の値です。試走結果とペダルとの比で目標を補正し、設定を控えてから変更します。43番の初期目標はAが1,080パルス、Bが1,188パルスです。39番のツリー初期目標は合計360円板回転です。以前の書き込みで保存した設定が残る場合は config、status で確認します。

演出は軌跡、車体と翼、ツリー、ドレープ、外周の順です。実物ボタンで始まらないときと次の照明が始まらないときは、担当者が目視して予備開始を使います。終演後は定常運転に切り替えます。39番の現地ボタンかシリアルの stop で消灯します。通信が戻った場合も消灯を確認してから改めて arm します。

会場ルーターを使う場合は2.4 GHz・チャンネル6を確認します。39番のUSBシリアルで site status を確認し、現地で登録した駅前点灯式の設定を使います。スマホを中継に使う場合は、39番の起動時にシリアルに出る接続先とパスワードでつなぎ、ESPが示すローカルURLの /view を前面で開きます。公開配布版の39・47番で作るアクセスポイントは配布用の名称を使い、パスワードは起動時に生成します。実際に使う回線の接続情報とSites送信設定は機材担当から個別に受け取ります。

スタッフ用端末は開演前の停止中に39番で site staff を送り、表示されたリンクを60秒以内に開きます。Sitesで予備開始するときは二段階の確認をします。取り消し可能な時間は約5秒です。[公開Sitesの本番進行](https://bicycle-illuminations-1128.feyhawthorn16.chatgpt.site/)

## 単体試験

| 番号 | 試験と使い方 |
| --- | --- |
| [04　翼LED](tests/04_led_basic/) | classic NanoのD6から直列抵抗を介して翼のDINへ。別系統の保護付き12 Vでテープを給電し、GNDを共通にする。色順と入力側を確認。 |
| [25　24 V照明1本](tests/25_tree_light_pwm/) | DevKitCと外付けの出力回路で、GPIO25の金・GPIO26の白を単独確認。A3のT1〜T4を試すときは本番40・41番を使う。 |
| [26　回転入力と無線送信](tests/26_wing_rpm_sender/) | 片側ずつ count zero → 円板1周 → count。回した側が6パルス増えるかを確認。 |
| [28　回転表示の8灯](tests/28_rider_bars_test/) | D1〜D8の順と色を、無線なしで確認。 |
| [30　軌跡・ドレープ](tests/30_drape_led_test/) | Rev B2のGPIO23出力許可、J3のDIN、5 V給電とGNDを照合。使う長さの灯数を設定して順送りを確認。 |
| [45　回転数の60秒計測](tests/45_nano_cadence_log/) | NanoのD2＝A、D3＝BにOUTを仮接続し、GNDを共通にする。センサーは既存の3.3 V給電。片側ずつ円板1周で6パルスを確認し、start で60秒記録。USBシリアルのSTART〜ENDを保存し、試験後は仮配線を外して44番に戻す。 |
| [46　スマホのテザリングへ接続](tests/46_wifi_station_sync/) | 2.4 GHzの会場用テザリングを用意。USBシリアルで ssid <Wi-Fi名>、次に pass <パスワード> を入力して status。各台で相手の台数分 roundtrip=OK を確認。 |
| [47　ESPのWi‑Fiへ接続](tests/47_wifi_access_point/) | ESPを起動し、シリアルに出た接続先にスマホをつなぐ。表示されたローカルURLを開いて 47 OK を確認。 |

46・47番は照明を駆動しません。自転車フレーム照明は39番の test a・test b、製作基板のツリー・外周は40・41番の test で確認します。単体試験のあとに各機器へ本番スケッチを書き戻してください。

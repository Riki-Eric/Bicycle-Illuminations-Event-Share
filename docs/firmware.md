# スケッチと実装対象

各ディレクトリで同名の `.ino` を開きます。共通名のヘッダーも各スケッチの近くに同梱しています。GitHubのZIPを取得した場合はフォルダーごと展開してください。

## 本番用

| 番号・スケッチ | 機器・ボード | 設営で決める項目 |
| --- | --- | --- |
| [27](../production/27_rider_bars_receiver/) | 回転表示、DevKitC V4 | 車体A/Bと8灯の向き |
| [38](../production/38_drape_finale_esp32/) | ドレープ、B2・DevKitC V4 | 箱ID 1＝A、2＝Bとテープの方向 |
| [39](../production/39_bike_opening_show/) | 進行、軌跡・車体、B2・DevKitC V4 | 軌跡の向き、J4＝A・J5＝B、外周箱数 |
| [40](../production/40_tree24_show/) | ツリー、A3・DevKitC V4 | ID 1＝下4段、2＝上3段、端子と段の対応 |
| [41](../production/41_plaza24_show/) | 外周、A3・DevKitC V4 | 固有の箱ID、各箱の使用本数 |
| [42](../production/42_mayor_button/) | 市長ボタン台 | ソースはDevKitのGPIO定義。XIAO現物ではGPIOと接点の照合が必要 |
| [43](../production/43_wing_opening_show/) | 翼A、Rev EのDevKitC V4または予備機のXIAO ESP32C3 | A/Bのセンサー・向き・目標、予備機は44番との方式 |
| [44](../production/44_wing_arduino_bridge/) | 翼の予備機、classic Nano | 43番に合わせてD8またはA0を選ぶ |

DevKitC V4はESP32-WROOM-32Eの構成です。翼の予備機は[対応する回路と面別配置](../wiring/README.md#翼のユニバーサル基板予備機)を確認します。

| 翼の構成 | 43番の `WING_USE_ARDUINO_BRIDGE` | 44番の `WING_USE_A0_COMPARATOR` |
| --- | --- | --- |
| Rev E / DevKitC V4 | 0（既定） | 44番を使用しない |
| 予備機D8 / XIAO ESP32C3 | 1（XIAOの既定） | 0（既定） |
| 予備機A0 / XIAO ESP32C3 | 2 | 1 |

## ボードと依存関係

| 対象 | Arduino IDE | Arduino CLIのFQBN |
| --- | --- | --- |
| DevKitC V4 | esp32 3.3.12、ESP32 Dev Module | `esp32:esp32:esp32` |
| XIAO ESP32C3 | esp32 3.3.12、XIAO_ESP32C3、USB CDC On BootをEnabled | `esp32:esp32:XIAO_ESP32C3:CDCOnBoot=cdc` |
| classic Nano | Arduino AVR Boards 1.8.8、Arduino Nano、ATmega328P | `arduino:avr:nano:cpu=atmega328` |

Nanoで旧ブートローダーが使われている場合は、現物に合わせてアップロード設定を選びます。ESP32のWi-Fi・HTTP・RMTはコア付属です。04番と44番のNeoPixelライブラリはライセンス込みで各 `src/` に入っています。

配布版ルートからのビルド例です。

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 production/39_bike_opening_show
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3:CDCOnBoot=cdc production/43_wing_opening_show
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328 tests/45_nano_cadence_log
```

[試験スケッチの用途](testing.md)と[コンパイルの確認範囲](validation.md)も参照してください。

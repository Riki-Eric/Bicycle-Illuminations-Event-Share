# 現行基板の配線図

配線を触る前に全電源を切ります。通電前に端子の印字、定格、電圧、極性と過電流保護を照合します。外部給電とUSBを同時に接続する場合は対象基板の給電条件を先に確認してください。

- [製造用データの表と裏。自転車・ドレープ、ツリー・広場外周、回転表示の3種類](diagrams/production-front-and-back.png)

## 自転車・ドレープ

**Bike-Drape Rev B2 TBT**　自転車側のESPとドレープA・Bで使う共通基板です。

| 項目 | 仕様 |
| --- | --- |
| ESP | ESP32-DevKitC V4（ESP32-WROOM-32E） |
| 基板の外形 | 172 × 74 mm（製造データ） |
| 電源 | J8：制御5 V。J2：LED用5 V。J1：自転車側の12 V。いずれもセンタープラス。 |
| 光の軌跡 | J3：1＝データ（GPIO18）、2＝GND、3＝LED用5 V。GPIO23はデータと12 V負荷の共通出力許可。 |
| ドレープ | A・Bそれぞれの基板でJ7を使用。1＝LED用5 V、2＝GND。 |
| 車体照明・操縦者照明 | J4＝アップライトA（GPIO19）、J5＝アップライトB（GPIO21）、J6＝操縦者照明（GPIO22）。12 Vの負側を制御。 |
| 入力・状態表示 | 現地入力GPIO34。RUN／LINK／SHOW／FAULTはGPIO25／26／27／32。 |

基板上の車種表記にかかわらず、今回のアップライトAをJ4・BをJ5に割り当てます。調光出力の戻りは各J4・J5の指定端子へつなぎます。ドレープA/Bの箱では自転車用12 V系を使用せず、J3のDINとJ7の5 V/GNDを使います。

- [部品面の設計図｜電源ジャック・照明端子・ESPの位置](diagrams/bike-drape-front.png)
- [はんだ面の設計図｜裏面の配線](diagrams/bike-drape-back.png)

## ツリー・広場外周

**Tree24 A3 端子表示付き**　ツリーの下段4系統・上段3系統と、広場外周の7系統で使う共通基板です。

| 項目 | 仕様 |
| --- | --- |
| ESP・出力IC | ESP32-DevKitC V4（ESP32-WROOM-32E）、TBD62083APG。 |
| 基板の外形 | 99.06 × 76.20 mm（製造データ） |
| 電源 | J5：制御5 V。J24：照明用24 V。いずれもセンタープラス。 |
| 照明端子 T1〜T4 | 各端子は1＝+24 V、2＝電球色（GOLD）、3＝白（WHITE）。4・5は未使用。 |
| GPIO（電球色／白） | T1：23／22、T2：21／19、T3：18／17、T4：16／4。 |
| 現地入力・調光 | 現地入力GPIO34。PWM 200 Hz。 |


- [製造プレビュー｜T1〜T4の端子表示](diagrams/tree24-terminal-labels.png)
- [A3の部品面の設計図｜T1〜T4と電源ジャックの位置](diagrams/tree24-front.png)
- [A3のはんだ面の設計図｜裏面の配線](diagrams/tree24-back.png)

## 自転車の回転表示

**RiderBar LED8 Rev E TBT**　アップライトA・Bに1枚ずつ置き、翼AのESPから届く回転数を8灯で表示します。

| 項目 | 仕様 |
| --- | --- |
| ESP・電源 | ESP32-DevKitC V4（ESP32-WROOM-32E）、USB 5 V。 |
| 基板 | 99.06 × 50.80 mm、2層FR-4、厚さ1.6 mm、外層1 oz。 |
| LED端子 | D1〜D8の順にGPIO18・19・21・22・23・25・26・27。各LEDに直列抵抗1本、カソードはすべてGND。 |
| 表示設定 | 初期設定は90 rpmで8灯すべて点灯。直列抵抗値は使用するLEDに合わせます。 |

部品面のD1→D8の順に緑・緑・黄・黄・黄・赤・赤・赤です。左右8灯を個別に試験します。

- [部品面の設計図｜D1〜D8と抵抗の並び。製造版の表裏は上の3種類比較に掲載](diagrams/rider-bar-front.png)

## 翼A

**翼A Rev E（設計・引き継ぎ版）**　翼のLEDテープ1本を左右別々に光らせ、2台の自転車の回転数と進捗を読み取ります。

| 項目 | 仕様 |
| --- | --- |
| 電源 | J1：翼A専用12 V、センタープラス。 |
| 翼LED | J5：1＝12 V、2＝DIN（GPIO18から5 Vへの信号変換を経由）、3＝GND。 |
| 回転センサー | FC-33を2個使用。J10・J11はいずれも3.3 V・OUT・GND。OUTはそれぞれGPIO34・35へ。 |

掲載しているのは引き継ぎ時のRev Eです。配線作業前に、製作担当のCADと現物の端子表示を照合してください。

- [基板レイアウト｜LED出力と回転センサー端子](diagrams/wing-a-layout.png)
- [回路図｜LED出力・電源・FC-33入力](diagrams/wing-a-schematic.png)

## 翼のユニバーサル基板予備機

**XIAO ESP32C3＋classic Nano**　回転センサー2個をXIAOで受け、Nanoから翼LEDへ出力します。D8とA0はどちらか一方を選びます。

| 項目 | 仕様 |
| --- | --- |
| 回転センサーXH | 部品面から左より3.3 V・OUT・GND。AのOUTはXIAO D4、BはD5。 |
| Nanoへの信号 | XIAO D10の反転UART。D8はTN0702N3-G、A0は内蔵コンパレータを使用。 |
| 給電 | XIAOとNanoは各USB、翼は別系統の12 V。信号GNDを共通にする。 |

43番はXIAO、44番はNanoに書き込みます。D8版は標準の設定1/0、A0版は定義 WING_USE_ARDUINO_BRIDGE=2 / WING_USE_A0_COMPARATOR=1 にそろえます。独立ランド基板の同じ方式の部品面とはんだ面を使います。各USBで制御側を給電し、翼12 Vの負荷電流は基板やNanoを通さず、保護を通して直接配線します。

- [D8・MOSFETの回路図](diagrams/wing-backup-d8-circuit.png)
- [D8版の部品面](diagrams/wing-backup-d8-front.png)
- [D8版のはんだ面](diagrams/wing-backup-d8-back.png)
- [A0・内蔵コンパレータの回路図](diagrams/wing-backup-a0-circuit.png)
- [A0版の部品面](diagrams/wing-backup-a0-front.png)
- [A0版のはんだ面](diagrams/wing-backup-a0-back.png)

## 市長ボタン台

**XIAO ESP32C3＋ユニバーサル基板**　ボタンの無電圧接点をESPが読み取り、開始を会場内へ無線で知らせます。

| 項目 | 仕様 |
| --- | --- |
| 市長ボタン | 無電圧の常開接点。XIAO側のGPIOは現物とファームウェアを照合して確定。 |
| 予備ボタン・状態LED | XIAO側の接続先は機材担当が現地で確認。 |

XIAO版のGPIOと配線は現物確認待ち。
42番の現行ソースのDevKit用GPIOをXIAOに流用すると端子が一致しません。現物の接点・状態LEDと対応するGPIOを確認してから、42番のピン定義と配線を確定してください。


[書き込み・設営の手順](../README.md)

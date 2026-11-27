# シリアルコマンド

本番機のUSBシリアルは **115200 bps・改行あり**。まず該当機器の `help` を開きます。04番のような1文字入力の試験には[試験スケッチ冒頭](../tests/04_led_basic/04_led_basic.ino)の案内が適用されます。

## 表示と位置確認

| 機器 | 表示する操作 | 照明・接点の試験 |
| --- | --- | --- |
| 27 回転表示 | `status`で受信経過とA/BのRPM、`role`で車体 | `test 1`は緑側、`test 8`は赤側、`test`で順送り。終了後は`p`で回転表示 |
| 38 ドレープ | `status`で箱ID、通信、進行、出力 | `mark start`、`mark middle`、`mark end`。それぞれ始端・中央・末端を目視 |
| 39 軌跡・車体 | `status`で進行・各機器の受信経過とID重複、`site status`でSites送信 | `mark start`でボタン側、`mark end`で自転車側。 `test a`はJ4、`test b`はJ5 |
| 40 ツリー | `map`でT端子と段、`status`で金・白のPWM指令 | `test 1`でT1を金→白。使用する端子を順に1本ずつ目視 |
| 41 外周 | `map`で箱ID・使用端子、`status`で出力指令 | `test 1`でT1を金→白。光った場所と箱・端子を記録 |
| 42 市長ボタン台 | `status`で生の接点状態・開始の待ち・無線 | GPIO照合後に`test contact`で10秒間の押し・離し、`test`で状態LED |
| 43 翼制御 | `map`でDINからの位置、`status`で回転・進捗、`count zero` → `count`で試験中の増分 | `mark a root`、`mark a tip`、`mark b root`、`mark b tip`で付け根と先端 |
| 44 翼予備機のNano | `status`でD8/A0の選択、受信許可、受信・エラー件数 | 43番で目印を点け、44番の受信と翼LEDを同時に確認 |

目印やテストには実際の出力を使います。39番をOFFにしてから対象機器を試し、表の操作が済んだら消灯を確認してください。42番のXIAOは[現物照合待ち](../wiring/README.md#市長ボタン台)です。

## 設定と進行

- 対応するスケッチの`watch 1000`は1秒ごとの表示、`watch off`は表示停止。 `config`は設定の確認、`config export`は復元用の行を出す。
- 設定の変更は停止中。 `set <項目> <値>`の項目名・単位・範囲をスケッチと照合する。箱IDやA/Bの保存は機器ごとに手順が異なる。
- 39番の`arm`は開演待ち、`start`は待機中の開始、`steady`は終演後の定常点灯。 `stop`は公演や試験を止めて消灯を指示する。本番の手順は[設営・本番操作](operations.md)。
- ログを人やAIに渡すときは[共有する項目](ai-handoff.md)を参照する。

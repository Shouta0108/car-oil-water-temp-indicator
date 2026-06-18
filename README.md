# car-oil-water-temp-indicator

自動車向けの自作追加メーター（油温・水温計）。Raspberry Pi Pico (RP2040) と 3.5インチ ILI9486 ディスプレイを使い、アナログメーター風の **OIL TEMP / WATER TEMP** を2連で表示します。

![ゲージ表示イメージ](docs/gauge.png)

> 表示イメージ画像（`docs/gauge.png`）は任意です。スクリーンショットを置く場合はこのパスに配置してください。

## 特徴

- **2連アナログゲージ**: 水温（左）と油温（右）を1画面に並べて表示
- **起動アニメーション**: 電源投入後、針が一往復するオープニング演出（約4.4秒）
- **レッドゾーン警告**: 設定温度（既定 115 ℃）を超えるとゲージ外周が赤く点滅
- **ブザー警報**: 警告温度で断続音、上限温度（130 ℃）超過で連続音
- **平滑化フィルタ**: ADC 値・針角度に一次フィルタ（`FILTER_BETA = 0.05`）を掛けてちらつきを抑制
- **デュアルコア駆動**: コア0でセンサー読み取り・状態管理、コア1で描画（`setup1` / `loop1`）

## ハードウェア構成

| 部品 | 内容 |
| --- | --- |
| マイコン | Raspberry Pi Pico (RP2040) |
| ディスプレイ | 3.5インチ ILI9486 SPI LCD（480×320 / 寸法 85.5×55.6 mm） |
| 温度センサー | サーミスタ ×2（水温・油温）。10 kΩ 直列抵抗で分圧 |
| 警報 | パッシブ／アクティブブザー |

### サーミスタの定数（コード内の換算式）

`updateSensorData()` のスタインハート–ハート近似で以下を前提にしています。必要に応じて使用するサーミスタに合わせて調整してください。

- 直列抵抗: 10 kΩ
- 基準抵抗 R0: 11 kΩ（20 ℃時）
- B 定数: 3500

## ピンアサイン

スケッチ内で定義しているピンは以下のとおりです（GPIO 番号）。

| 機能 | ピン | 定義 |
| --- | --- | --- |
| 水温センサー (ADC0) | GPIO 26 | `SENSOR_WATER` |
| 油温センサー (ADC1) | GPIO 27 | `SENSOR_OIL` |
| 切り替えスイッチ | GPIO 28 | `SW_PIN` |
| ブザー | GPIO 20 | `BUZZER_PIN` |
| LCD バックライト | GPIO 16 | `TFT_BL` |

> ディスプレイの SPI ピン（MOSI / SCK / CS / DC / RST など）は **TFT_eSPI の `User_Setup.h`** 側で設定します。ILI9486 RPi LCD の端子は +5V / GND / DC / RST / CS / MOSI / SCK / MISO / T_CS（タッチ未使用時は 3.3V へ）。

## 必要なライブラリ

- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)（ILI9486 ドライバ・スプライト描画）

`User_Setup.h`（または `User_Setup_Select.h`）で **ILI9486 ドライバ**・解像度・Pico の SPI ピンを有効化してください。フォントは同梱の `eurostile_extd_black_italic8pt7b.h` を使用します。

## ビルドと書き込み（Arduino IDE）

1. Arduino IDE に **Raspberry Pi Pico / RP2040** ボードパッケージ（earlephilhower 版）を追加
2. TFT_eSPI ライブラリをインストールし、`User_Setup` を ILI9486 + Pico 用に設定
3. ボードに「Raspberry Pi Pico」、適切なポートを選択
4. `indicator_test.ino` を開いて書き込み

## ファイル構成

```
indicator_test/
├── indicator_test.ino                 # メインスケッチ（デュアルコア）
├── eurostile_extd_black_italic8pt7b.h  # 表示用カスタムフォント (8pt)
├── eurostile_extd_black_italic10pt7b.h # 表示用カスタムフォント (10pt)
└── README.md
```

## 主な設定値（スケッチ冒頭で変更可能）

| 定数 | 既定値 | 内容 |
| --- | --- | --- |
| `WATER_REDZONE_TEMP` | 115.0 | 水温レッドゾーン開始温度 [℃] |
| `OIL_REDZONE_TEMP` | 115.0 | 油温レッドゾーン開始温度 [℃] |
| `MAX_GAUGE_TEMP` | 130.0 | ゲージ上限・連続警報温度 [℃] |
| `FILTER_BETA` | 0.05 | 平滑化フィルタ係数（小さいほど滑らか） |
| `CENTER_X` / `CENTER_X2` | 114 / 356 | 左右ゲージの中心 X 座標 |

## 参考資料

- [TMP36 を用いた温度の計測と LCD への表示 — 基礎からの IoT 入門](https://iot.keicode.com/arduino/temperature-tmp36.php)
- [raspberry pi で車のメーターを作成する — Qiita](https://qiita.com/ototo/items/ddeff02151890e2f6046)
- [File to C style array converter](https://notisrac.github.io/FileToCArray/)（画像→C配列変換。RGB565 / Big-endian / uint16_t）
- 市販追加メーター寸法の目安: φ52 / φ60 mm、Defi advance ZD ディスプレイ 51.8×26.7 mm

## メモ

開発の経緯では MicroPython + Pico 向けに ILI9486 へ JPEG を描画するカスタムモジュール（`ili9486_jpg_display`）も検討しましたが、本リポジトリの最終形は **Arduino + TFT_eSPI** によるスプライト描画版です。

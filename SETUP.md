# pico-basic 開発環境構築ガイド (macOS)

このドキュメントでは、macOS（Apple Silicon / M1〜M4）上で **pico-basic** を実機（Raspberry Pi Pico 2 / RP2350）向けにビルドし、書き込むための手順を解説します。

複雑なツールチェーンの個別インストールは不要です。Visual Studio Codeの公式拡張機能を利用して、簡単に環境を構築できます。

## 1. 必要なもの

* **Mac** (Apple Silicon M1/M2/M3/M4推奨)
* **Raspberry Pi Pico 2** (または Waveshare RP2350-Touch-LCD-2.8 等の互換ボード)
* **USBケーブル** (データ通信対応のもの。充電専用ケーブルではPCに認識されません)

---

## 2. 開発ツールのインストール

### 2.1 Visual Studio Code (VS Code) のインストール
すでにインストール済みの場合はスキップしてください。
1. [VS Code公式サイト](https://code.visualstudio.com/) にアクセスします。
2. 「Mac OS」用のインストーラー（Apple Silicon対応版）をダウンロードし、インストールします。

### 2.2 Raspberry Pi Pico 公式拡張機能のインストール
この拡張機能が、コンパイラ（C/C++をPico用に変換するツール）やPico SDKなどの必要なものを全て自動で用意してくれます。

1. VS Codeを起動します。
2. 左側のメニューからブロックのアイコン「拡張機能 (Extensions)」をクリックします。
3. 検索バーに `Raspberry Pi Pico` と入力します。
4. 提供元が「Raspberry Pi」となっている公式の拡張機能を見つけ、**「インストール (Install)」** をクリックします。

---

## 3. プロジェクトのセットアップ

### 3.1 リポジトリのクローン
Macの「ターミナル」を開き、任意のディレクトリで以下のコマンドを実行してプロジェクトをダウンロードします。

```bash
git clone https://github.com/Toshiaki0315/pico-basic.git
cd pico-basic
```

> **リポジトリ URL について:** GitHub 上のリポジトリ名が `pico-basic` でない場合（例: `pico-hubasic` のままの場合）は、上記 `git clone` の URL を実際のリポジトリに合わせてください。クローン後のフォルダ名が `pico-basic` でなくても、以降の手順ではそのフォルダを開けば問題ありません。

### 3.2 VS Codeでの読み込みと初期設定
1. VS Codeのメニューから `ファイル (File)` > `フォルダを開く (Open Folder)` を選び、先ほどクローンした `pico-basic` フォルダを開きます。
2. フォルダを開くと、左側のメニューに「Raspberry Piのロゴ」のアイコンが追加されています。これをクリックします。
3. 初回のみ、拡張機能がPico SDKやツールチェーンの自動ダウンロードを行います。画面の指示に従って完了するまでお待ちください。

### 3.3 Pico SDK を手動で用意する（`pico_sdk_import.cmake` が無い場合）

リポジトリルートに `pico_sdk_import.cmake` が無いと、コマンドラインの `cmake` だけでは Pico 向けビルドが開始できません。次のいずれかで対応できます。

1. **VS Code の Raspberry Pi Pico 拡張を使う（推奨）**  
   拡張が SDK とツールチェーンを取得します（本ドキュメントのセクション 2.2）。

2. **Pico SDK を手動クローンする**  
   - [Raspberry Pi pico-sdk](https://github.com/raspberrypi/pico-sdk) を任意のパスに clone する。  
   - シェルで `export PICO_SDK_PATH=/path/to/pico-sdk` を設定する（永続化するなら `~/.zshrc` 等へ）。  
   - SDK 付属の [`external/pico_sdk_import.cmake`](https://github.com/raspberrypi/pico-sdk/blob/master/external/pico_sdk_import.cmake) を **プロジェクトルート**（`CMakeLists.txt` と同じ階層）にコピーする。  
   - 公式の入門資料: [Getting started with Raspberry Pi Pico-series](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf)（PDF）および [Raspberry Pi ドキュメント](https://www.raspberrypi.com/documentation/microcontrollers/raspberry-pi-pico.html)。

---

## 4. ビルド（コンパイル）の手順

コードをPico 2が理解できる形式（`.uf2`ファイル）に変換します。

1. VS Codeの下部（ステータスバー）にある **「Compile Project」** をクリックします。
   * ※もしターゲットボードを聞かれた場合は `pico2` または `rp2350` を選択してください。
2. コンパイルが始まります。エラーが出なければ成功です。
3. 成功すると、プロジェクトフォルダ内に `build` というフォルダが作成され、その中に `pico_basic.uf2` というファイルが生成されます。

---

## 5. 実機への書き込み（フラッシュ）

生成されたプログラムをPico 2本体に転送します。

1. Pico 2本体にある **「BOOT」ボタン（または BOOTSEL ボタン）を押したまま**、MacにUSBケーブルで接続します。
2. 接続したらボタンから指を離します。
3. Macのデスクトップ（またはFinder）に `RPI-RP2` という名前のUSBメモリのようなドライブが表示されます。
4. 先ほどコンパイルして作成された `build/pico_basic.uf2` ファイルを、この `RPI-RP2` ドライブへ **ドラッグ＆ドロップ** してコピーします。
5. コピーが完了すると、ドライブは自動的に取り外され、Pico 2上でプログラムが自動的に実行（再起動）されます。

---

## 6. 動作確認（シリアルモニタリング）

プログラム内の `printf()` などによる出力結果（エラーログやデバッグ情報）をMac上で確認する方法です。

1. プログラムが書き込まれ、Pico 2がMacとUSB接続された状態にしておきます。
2. VS Code下部のステータスバーにある **「Pico: Serial Monitor」** （プラグアイコンのようなもの）をクリックします。
3. ポート一覧から、Pico 2に該当するもの（例: `/dev/cu.usbmodemXXXX`）を選択します。
4. VS Codeの下部にシリアルコンソールが開き、Pico 2からのテキスト出力が確認できるようになります。

---

## 7. ハードウェア仕様・ピンアサイン (Pinout)

本プロジェクトがメインターゲットとしている `Waveshare RP2350-Touch-LCD-2.8` の内部ピンアサインです。
ボードに直付けされている液晶やSDカードスロットは、内部的に以下のRaspberry Pi Pico（RP2350）のGPIOピンに接続されています。

別のSPIディスプレイをジャンパワイヤで配線してテストする場合や、C++側でハードウェアの初期化コードを記述（デバッグ）する際の参考にしてください。

### 7.1 SPI 液晶ディスプレイ (ST7789T3)
| ピンの役割 | 信号名 (LCD側) | Pico 2 GPIOピン | 備考 |
| :--- | :--- | :--- | :--- |
| **MOSI** (データ送信) | LCD_MOSI | **GPIO 11** | SPI1 TX |
| **MISO** (データ受信) | LCD_MISO | **GPIO 12** | SPI1 RX (通常の描画では使用しない) |
| **SCK** (クロック) | LCD_SCK | **GPIO 10** | SPI1 SCK |
| **CS** (チップセレクト) | LCD_CS | **GPIO 13** | |
| **DC** (データ/コマンド切替)| LCD_D/C | **GPIO 14** | |
| **RST** (リセット) | LCD_RST | **GPIO 15** | |
| **BL** (バックライト) | LCD_BL | **GPIO 16** | PWM制御可能 |

### 7.2 タッチパネル (CST328 - I2C接続)
| ピンの役割 | 信号名 | Pico 2 GPIOピン | 備考 |
| :--- | :--- | :--- | :--- |
| **SDA** (データ) | TP_SDA | **GPIO 6** | I2C1 SDA |
| **SCL** (クロック) | TP_SCL | **GPIO 7** | I2C1 SCL |
| **INT** (割り込み) | TP_INT | **GPIO 18** | タッチ検知用 |
| **RST** (リセット) | TP_RST | **GPIO 17** | |

### 7.3 MicroSDカードスロット (SPI接続)
BASICプログラムのセーブ/ロード（`CAS:` や `0:` ドライブ）に使用します。

| ピンの役割 | 信号名 (SD側) | Pico 2 GPIOピン | 備考 |
| :--- | :--- | :--- | :--- |
### 7.3 MicroSDカードスロット
BASICプログラムのセーブ/ロード等に使用します。標準的なSPIモードのほか、PIOを用いたSDIO(4bit)アクセスも物理的に可能です。

| ピンの役割 (SPI) | 信号名 (SD側) | Pico 2 GPIOピン | 備考 |
| :--- | :--- | :--- | :--- |
| **SCK** (クロック) | SD_SCK | **GPIO 19** | SPI0 SCK |
| **MOSI** (データ送信) | SD_CMD | **GPIO 20** | SDIOのCMDピンをSPIのMOSIとして使用 |
| **MISO** (データ受信) | SD_D0 | **GPIO 21** | SDIOのD0ピンをSPIのMISOとして使用 |
| - (SPIでは未使用) | SD_D1 | **GPIO 22** | SDIO 4bit通信用データ線1 |
| - (SPIでは未使用) | SD_D2 | **GPIO 23** | SDIO 4bit通信用データ線2 |
| **CS** (チップセレクト) | SD_D3 | **GPIO 24** | SDIOのD3ピンをSPIのCSとして使用 |

※ C++側で `TFT_eSPI` やカスタムのSPIドライバを記述する場合は、上記のGPIO番号をコンフィグに指定してください。

---

## 8. ホスト単体テスト（`BUILD_TESTS=ON`）

Pico 実機や Pico SDK が無くても、PC 上で lexer/parser と HAL モックを Google Test で検証できます。

### 8.1 必要なもの

- CMake 3.14 以上
- C++20 対応コンパイラ（Clang / GCC / MSVC など）
- インターネット接続（初回ビルドで Google Test を FetchContent 取得）

### 8.2 ビルドとテスト実行

リポジトリルートで次を実行します（ビルドディレクトリ名は任意です）。

```bash
cmake -S . -B build_host -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build_host --parallel
./build_host/basic_tests
```

`basic_tests` が Google Test のエントリになり、登録されたテストを一括実行します。  
`ctest` を使う場合はビルドディレクトリで `ctest --output-on-failure` も利用できます（プロジェクト設定によりテストが CTest に登録されている場合）。

### 8.3 CI

GitHub 上では `.github/workflows/ci.yml` が同様に `BUILD_TESTS=ON` でビルドし、`basic_tests` を実行します。

### 8.4 品質ドキュメント（失敗時フロー・実機チェック）

- **[docs/TESTING.md](./docs/TESTING.md)** … テストの定期実行、`--gtest_filter`、CI 失敗時の修正フロー
- **[docs/DEVICE_CHECKLIST.md](./docs/DEVICE_CHECKLIST.md)** … USB / LCD / SD 等の手動確認チェックリスト

---

**トラブルシューティング**
* **MacがPico 2を認識しない (`RPI-RP2` が出ない):** ケーブルがデータ通信対応か確認してください。また、BOOTボタンを確実に押しながら接続しているか再度確認してください。
* **コンパイルエラーが出る:** `CMakeLists.txt` の設定や、ソースコードの文法エラーがないか、VS Codeの「問題 (Problems)」タブを確認してください。
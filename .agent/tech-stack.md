# Technology Stack: pico-basic

このプロジェクトで使用されている技術スタックと、開発環境の制約事項です。

## 1. Core Hardware
- **MCU**: Raspberry Pi Pico 2 (RP2350)
- **Architecture**: ARM Cortex-M33 (Dual-core)
- **Memory**: 520KB SRAM / 4MB External Flash (標準構成)

## 2. Programming Language & Tools
- **Language**: C++20
    - `std::vector` などの動的コンテナの使用は極力避け、`std::array` や固定長バッファを使用すること。
    - 浮動小数点演算は RP2350 のハードウェア FP ユニットを活用する。
- **SDK**: Raspberry Pi Pico SDK (Latest version)
    - `pico_stdlib`, `hardware_pio`, `hardware_dma`, `hardware_pwm` を主に使用。
- **Build System**: CMake (Minimum 3.13)
- **Compiler**: arm-none-eabi-gcc

## 3. Libraries & Dependencies
- **FatFs**: SDカードからの `LOAD/SAVE` 用（SPI接続）。
- **st7789_library**: 液晶ディスプレイ表示用（またはプロジェクトで使用中のドライバ名）。
- **Unity / GoogleTest**: `tests/` ディレクトリでの PC 上のエミュレーション実行用（TDD用）。

## 4. Hardware Mapping (想定)
- **UART**: デバッグ出力（printf）に使用。
- **GPIO**:
    - SD Card: SPI0 (MISO: Pin X, MOSI: Pin Y, SCK: Pin Z)
    - Display: SPI1 (CS: Pin A, DC: Pin B)
- **PIO (Programmable I/O)**: ビデオ信号生成またはキーボード（PS/2等）のプロトコル解析に使用予定。

## 5. Development Command Reference
エージェントが実行可能な（または提案すべき）コマンド：
- **ビルド**: `mkdir -p build && cd build && cmake .. && make`
- **テスト実行**: `cd build/tests && ctest`
- **フラッシュ**: `picotool load -x build/pico_basic.uf2`

## 6. Constraints & Guidelines
- **Heap Usage**: 原則禁止。起動時に静的にメモリを確保するスタイルを推奨。
- **Multicore**: Core 0 を BASIC インタプリタ、Core 1 を描画やサウンド処理に割り当てる設計を目指す。
- **Floating Point**: Hu-BASIC の仕様に合わせ、単精度/倍精度の切り替えを考慮する。

## 7. Specific Hardware Details
開発に使用するメインボードおよび周辺回路の情報です。

- **Main Board**: [Waveshare RP2350-Touch-LCD-2.8](https://www.waveshare.com/wiki/RP2350-Touch-LCD-2.8)
    - **MCU**: RP2350A (Dual-core ARM Cortex-M33 @ 150MHz)
    - **Display**: 2.8inch Touch LCD (320×240 pixels)
        - Driver IC: ST7789V
        - Interface: SPI
    - **Touch Controller**: CST328 (I2C interface)
    - **Storage**: MicroSD Card Slot (SDIO/SPI interface)
    - **Battery Management**: ETA6003 (Li-po battery header)

- **Reference Documents**:
    - [Schematic (PDF)](https://files.waveshare.com/wiki/RP2350-Touch-LCD-2.8/RP2350-Touch-LCD-2.8-Schematic.pdf)
    - [Product Page (Switch Science)](https://www.switch-science.com/products/10331)

- **Key Pin Assignment（回路図 RP2350-Touch-LCD-2.8-Schematic.pdf の対応表より）**:
    - **LCD**: SCK=GP10, MOSI=GP11, MISO=GP12, CS=GP13, D/C=GP14, RST=GP15, BL=GP16
    - **Touch (CST328)**: TP_SDA=GP6, TP_SCL=GP7, TP_RST=GP17, TP_INT=GP18
    - **MicroSD (SDIO)**: SD_SCK=GP19, SD_CMD=GP20, SD_D0=GP21, D1=GP22, D2=GP23, D3=GP24
    - **I2S (PCM5101A)**: BCK=GP2, LRCK=GP3, DIN=GP4
    - **UART**: TXD=GP0, RXD=GP1
    - **Battery**: KEY_BAT=GP25, BAT_EN=GP26, **BAT_ADC=GP27 (ADC1)**
      - 分圧は VBAT --200K-- BAT_ADC --100K-- GND なので **VBAT = ADC 電圧 x 3**
      - **BAT_EN (GP26) は電源ラッチの制御線**。誤って駆動すると電源が落ちうるので触れないこと
    - 空き: GP5, GP8, GP9, GP28, GP29
# PlatformIO + Raspberry Pi Pico SDK 設定研究

檢視日期：2026-10-17  
狀態：工具鏈選型與設定範本；未在本專案安裝或建置驗證。

## 決定

本專案韌體採 PlatformIO 專案工作流，直接使用 Raspberry Pi Pico C/C++ SDK APIs，選擇 `framework = picosdk`，不以 Arduino sketch 或 Arduino display library 作為基礎。

有一項重要限制：截至檢視日期，PlatformIO 官方 Raspberry Pi platform 的 Pico 文件列出 Arduino framework，官方平台的原生 Pico SDK 支援請求仍是 open issue。社群維護的 [maxgerhardt/platform-raspberrypi](https://github.com/maxgerhardt/platform-raspberrypi) fork 有 `picosdk-blink` 範例，並在 `platform.json` 加入 `picosdk` builder 與 SDK package。因此這條路能維持 PlatformIO workflow，但底層整合不是官方 Raspberry Pi platform 的承諾。[官方 Pico board 文件](https://docs.platformio.org/en/latest/boards/raspberrypi/pico.html)、[官方支援請求](https://github.com/platformio/platform-raspberrypi/issues/86)、[社群 `picosdk` 範例設定](https://github.com/maxgerhardt/platform-raspberrypi/blob/develop/examples/picosdk-blink/platformio.ini)、[fork 平台 manifest](https://github.com/maxgerhardt/platform-raspberrypi/blob/develop/platform.json)

## 設定草案

建立 firmware scaffold 時，先以最小環境開始：

```ini
[env:pico]
platform = https://github.com/maxgerhardt/platform-raspberrypi.git
board = pico
framework = picosdk
```

這是設定形狀，不是已在本專案驗證的完整建置。`picosdk-blink` 範例採用相同 board/framework 組合並由 `pio run` 建置。先新增最小 `src/main.cpp`，確認產出 UF2，再逐步加入 ADC、DMA、SPI 和 CDC 模組。

```sh
pio run -e pico
```

實際接板後再依除錯方式設定上傳：BOOTSEL/`picotool` 或 SWD probe。不要先假設板上有除錯器；標準 Raspberry Pi Pico 沒有內建 SWD probe。[PlatformIO Pico board 文件](https://docs.platformio.org/en/latest/boards/raspberrypi/pico.html)

## 版本固定和風險控制

- 起始 URL 指向社群 fork 的預設分支，會隨時間移動。第一個最小工程成功建置後，應把 platform URL 固定到已驗證的 git commit，並記錄 PlatformIO Core、framework package 與 Pico SDK commit。
- 社群 fork 當前 manifest 把 `framework-picosdk` 指向一個 `maxgerhardt/pico-sdk` snapshot commit。實際建置時要記錄該 snapshot 對應的 SDK 版本；不要只依賴「Pico SDK」名稱推斷版本或來源。
- 保留可重現的最小建置作為工具鏈驗收，再加入示波器功能。升級 PlatformIO 平台、framework package 或 SDK 時重新執行同一個基礎建置。
- Pico SDK 模組和 LCD driver 優先選原生 C/C++ SDK 相容版本。若使用依賴 Arduino `setup()/loop()` 或 `TFT_eSPI` 的 library，要先判斷它是否把程式拉回 Arduino framework；不要混淆 Arduino-Pico 與原生 `picosdk` 環境。
- USB CDC 量測資料與除錯輸出仍須分離。平台工具可提供 serial monitor，但其輸出不應和二進位 scope frame 共用解析通道。

## 備選路徑

| 路徑 | 評估 |
|---|---|
| PlatformIO + maxgerhardt `framework = picosdk` | **首選**：最貼近使用者指定的 PlatformIO 和原生 Pico SDK APIs；代價是依賴社群整合，須固定版本並驗收。 |
| PlatformIO + Arduino-Pico (`framework = arduino`, `board_build.core = earlephilhower`) | 可在 Arduino runtime 中呼叫許多 Pico SDK APIs，但不是本專案選定的 bare SDK build。`picOOscilloscope` 使用這條路，可參考 PIO pin/library 設定。 |
| PlatformIO + Wiz-IO `framework = baremetal` | 也提供 Pico SDK based bare-metal 平台；作者自述為 beta/work in progress，列為備援，不先作主線。 |
| 官方 PlatformIO `platform = raspberrypi` | 原生 Pico SDK 支援尚未出現在當前官方 board 文件/issue 狀態；不要在沒有建置驗證時把它寫成 `picosdk` 已官方支援。 |

## 尚待驗收

1. 在新 PlatformIO 專案中確認上述設定能解析 framework 並成功編出 UF2。
2. 記錄實際 SDK commit、compiler 版本和 `pio pkg list` 結果，然後固定 platform git commit。
3. 確認 `hardware_adc`、`hardware_dma`、`hardware_spi` 以及 TinyUSB CDC 的依賴/連結方式。
4. 只有完成最小 build 後，才將 `platformio.ini` 移到專案根目錄作為正式建置入口。

本文件目前只記錄經來源檢視的設定方向；沒有執行 `pio run`，也沒有宣稱 UF2 已能編出。

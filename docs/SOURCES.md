# 實作研究來源

檢視日期：2026-10-17。官方文件用來確認 SDK／硬體／瀏覽器合約；O-SCOPE 論文及程式只作參考實作，不視為已獨立驗證的量測結果。

## PlatformIO + Pico SDK 支援狀態

| 來源 | 內容與本專案用途 |
|---|---|
| [PlatformIO Raspberry Pi Pico board 文件](https://docs.platformio.org/en/latest/boards/raspberrypi/pico.html) | 官方 board 文件列出 `platform = raspberrypi`、`board = pico`；截至檢視日文件所列 framework 並未提供原生 Pico SDK。 |
| [官方平台的 Pico SDK 支援請求](https://github.com/platformio/platform-raspberrypi/issues/86) | 截至檢視日為 open issue；說明原生 SDK framework 不應假設已由官方平台提供。 |
| [maxgerhardt PlatformIO 平台 fork](https://github.com/maxgerhardt/platform-raspberrypi) | 社群 fork 的 `develop` 有 `framework = picosdk` 範例；其 `platform.json` 把 `picosdk` 指向 Pico SDK package。這是本專案目前最直接的 PlatformIO 原生 SDK 路徑，但不是官方平台保證。 |
| [PlatformIO Pico SDK blink 設定](https://github.com/maxgerhardt/platform-raspberrypi/blob/develop/examples/picosdk-blink/platformio.ini) | 展示 `framework = picosdk`、Pico board 設定和 `pio run` 工作流；用來起草本專案的設定，不代表本專案已成功建置。 |
| [Wiz-IO wizio-pico](https://github.com/Wiz-IO/wizio-pico) | 另一個直接使用 Pico SDK 的 PlatformIO 平台；作者標示為 beta／work in progress，故只列為備援調查對象，非首選工具鏈。 |
| [PlatformIO Arduino-Pico 設定文件](https://github.com/earlephilhower/arduino-pico/blob/master/docs/platformio.rst) | 展示以 Arduino-Pico core 在 PlatformIO 中使用 SDK APIs 的路徑；此路徑仍是 Arduino framework，本專案目標為原生 `picosdk`。 |

## O-SCOPE 參考實作

| 來源 | 內容與本專案用途 |
|---|---|
| [O-SCOPE arXiv v1](https://arxiv.org/abs/2610.18906v1) | 說明 RP2 的 MicroPython、ADC/DMA、Web Serial、Canvas 和 GitHub Pages 流程。論文報告 2.5 MS/s 時使用 240 MHz 系統時脈；本專案以 RP2040 官方 500 kS/s 規格作 v1 基準。 |
| [O-SCOPE GitHub snapshot](https://github.com/mandrecut/o_scope/commit/fc3c95a8090a39670ce01c78a890f21adcd6cfaf) | 本機檢視的 main snapshot，commit fc3c95a8090a39670ce01c78a890f21adcd6cfaf，日期 2026-09-05。根目錄有網頁、minified JavaScript 與 RP2040/RP2350 UF2；沒有可直接修改的韌體原始碼。 |
| [O-SCOPE HTML](https://github.com/mandrecut/o_scope/blob/fc3c95a8090a39670ce01c78a890f21adcd6cfaf/index.html) | 包含兩個通道、FFT、觸發、游標、WFG 和 PWM 控制；可借鑑功能，但不適合直接縮到 240×320 本機面板。 |
| [O-SCOPE browser JavaScript](https://github.com/mandrecut/o_scope/blob/fc3c95a8090a39670ce01c78a890f21adcd6cfaf/assets/index.min.js) | 使用 Web Serial；瀏覽器送 JSON 行、接收文字數據。新專案建議採模組化前端與有 framing/CRC 的資料協定。 |

## 其他參考專案

| 來源 | 內容與本專案用途 |
|---|---|
| [jklomp/pico-RP2040-oscilloscope](https://github.com/jklomp/pico-RP2040-oscilloscope) | README 描述 Pico C + Pico SDK firmware、USB serial、HTML/JavaScript Web Serial UI 和低頻 scope。適合參考 USB browser bridge；沒有本機 ST7789 UI。 |
| [AgainPsychoX/picOOscilloscope](https://github.com/AgainPsychoX/picOOscilloscope) | README/`platformio.ini` 顯示 PlatformIO、RP2040、ST7796 480×320、XPT2046；PlatformIO environment 用 `framework = arduino` 和 Earle Philhower core，因此是 PlatformIO/顯示分工參考，不是原生 `picosdk` 的範本。專案標示 work in progress。 |
| [stienman/Raspberry-Pi-Pico-PIO-LCD-Driver](https://github.com/stienman/Raspberry-Pi-Pico-PIO-LCD-Driver) | ST7789 320×240 LCD 和 XPT2046 touch 的 PIO/DMA driver，標示 BSD-2-Clause。專案說明會用六個 DMA channels 並佔用 PIO0，故只作顯示效能參考，不建議 v1 整份採用。 |
| [martinbogo/hadosoku](https://github.com/martinbogo/hadosoku) | Pico W/MicroPython scope，以 Wi-Fi HTTP/WebSocket 服務瀏覽器；包含無硬體 demo simulator、trigger/roll mode 和明列的 aliasing/input limits，可參考 UI demo 和故障提示，不能當成 USB/Pico SDK 實作。 |

詳細的「借鑑什麼／不能照搬什麼」及本專案整合決策見 [參考專案比較](REFERENCE_PROJECTS.md)。

## Raspberry Pi 與 ST7789

| 來源 | 可支持的結論 |
|---|---|
| [Pico C/C++ SDK 文件](https://www.raspberrypi.com/documentation/pico-sdk/) | 查閱時文件顯示 SDK 2.3.1；提供 C/C++、CMake、硬體 APIs。這是官方文件的版本資訊，不等同於 PlatformIO 社群 `picosdk` package 實際解析出的 SDK commit。 |
| [RP2040 hardware ADC API](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_adc) | ADC 500 kS/s、12-bit、RP2040 約 8.7 ENOB、四個外部 ADC input、FIFO 和 DMA。只有一個 ADC，通道選擇是多工器。 |
| [官方 ADC DMA 範例](https://github.com/raspberrypi/pico-examples/blob/master/adc/dma_capture/dma_capture.c) | 示範 ADC FIFO、DREQ_ADC、DMA 到 buffer、完成後停止 ADC 和清除 FIFO。 |
| [官方 ST7789 PIO 範例](https://github.com/raspberrypi/pico-examples/blob/master/pio/st7789_lcd/st7789_lcd.c) | 可參考初始化序列、RGB565 與 PIO 傳輸；這是高速展示範例，不代表低更新量 UI 一定要用 PIO。 |
| [Sitronix ST7789V controller datasheet](https://newhavendisplay.com/content/datasheets/ST7789V.pdf) | 控制器資料表標示 240RGB×320，並定義 address window 與 RAM write 命令。面板模組的腳位、offset、旋轉和背光仍要看實際模組資料表。 |

## USB、瀏覽器和部署

| 來源 | 可支持的結論 |
|---|---|
| [TinyUSB CDC API](https://github.com/hathach/tinyusb/blob/master/src/class/cdc/cdc_device.h) | 提供 CDC byte read/write API；實作可將除錯輸出與量測資料通道分開。 |
| [Chrome Web Serial 文件](https://developer.chrome.com/docs/capabilities/serial) | 網頁透過 navigator.serial 選擇、開啟和操作串列 port；requestPort 要由使用者操作觸發，資料以 Web Streams 傳送。 |
| [Web Serial 標準](https://wicg.github.io/serial/) | Web Serial 只對安全來源開放；需要使用 HTTPS 網站。 |
| [GitHub Pages 文件](https://docs.github.com/en/pages/getting-started-with-github-pages/what-is-github-pages) | 可從 GitHub repository 發佈靜態 HTML、CSS、JavaScript；不提供 Pico 與瀏覽器間的即時 backend relay。 |

## 證據界線

這次已檢視公開文件、O-SCOPE source tree、README、HTML 和 minified JavaScript。沒有反編譯 UF2、編譯本專案程式、連線 Pico、驅動 LCD、接函數產生器或量測 PCB。效能、波形準確度、輸入安全範圍、頻寬和面板腳位仍需要實測與模組資料表。

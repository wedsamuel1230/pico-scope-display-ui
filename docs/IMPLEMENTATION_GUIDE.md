# Pico Scope 韌體與網頁實作指南

研究日期：2026-10-17  
狀態：研究／架構草案；沒有韌體編譯、瀏覽器連線或實體量測證據。

## 建議的 v1 方向

- 韌體用 C/C++ 與 Raspberry Pi Pico SDK APIs，透過 PlatformIO 建置。PlatformIO 的原生 `picosdk` framework 目前需要社群維護的 Raspberry Pi 平台 fork；詳細設定和風險見 [PlatformIO 設定研究](PLATFORMIO_SETUP.md)。
- 先做一個 ADC 通道、邊緣觸發、單次擷取、ST7789 本機顯示及 USB 傳輸。
- ADC 使用 **hardware_adc + hardware_dma**，DMA 從 ADC FIFO 的 **DREQ_ADC** 請求搬資料。
- LCD 先用 **hardware_spi** 傳送局部畫面。PIO／SPI DMA 只在量測後證明更新速度不足時再加入。
- 瀏覽器先用原生 JavaScript、Canvas 和 Web Serial，GitHub Pages 只負責靜態檔案。
- Pico 沒有瀏覽器連線時仍需能擷取、顯示及用本機按鍵操作。
- USB 資料通道使用有版本、長度和 CRC 的二進位封包；不要把 **printf** 訊息混進同一 CDC 通道。

PlatformIO 在此負責環境、建置、上傳和後續測試整合；韌體仍直接呼叫 Pico SDK 的 ADC、DMA、SPI 和 TinyUSB/C 標準介面，不以 Arduino sketch 作為架構。原生 `picosdk` 支援來自社群平台 fork，尚未在本專案安裝或編譯驗證。

O-SCOPE 論文示範 MicroPython、ADC/DMA、Web Serial 和 GitHub Pages，值得參考其整體資料路徑。論文使用 240 MHz 系統時脈並報告最高 2.5 MS/s；RP2040 官方規格列出 500 kS/s ADC。v1 應先按官方規格設計，不把超頻數據當成保證值。[RP2040 ADC 規格](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_adc)、[O-SCOPE 論文](https://arxiv.org/html/2610.18906v1)

其他 Pico scope 與 ST7789/LCD 專案的適用處及限制整理在 [參考專案比較](REFERENCE_PROJECTS.md)。其中較直接可參考的是 Pico SDK + Web Serial 的 jklomp 專案、PlatformIO + 本機 TFT 的 picOOscilloscope，以及 ST7789 PIO/DMA driver；它們的 firmware framework、面板控制器和 DMA/PIO 資源分配各不相同，不能整份直接移植。

## 系統資料流

探棒 → 保護與類比前端 → ADC0 / GPIO26 → ADC FIFO + DREQ_ADC → DMA 擷取緩衝區 → 觸發、校正、量測與像素降採樣 → ST7789 本機畫面  
同一份處理後資料 → USB CDC 封包 → Web Serial → Canvas 波形與操作介面  
按鍵／旋鈕 → 設定與擷取狀態機 → ADC、LCD 及（已連線時）USB

## 已知硬體限制

RP2040 官方規格為 12-bit ADC、最高 500 kS/s，文件列出約 8.7 ENOB。它只有一個 ADC 核心；四個外部 ADC 輸入由同一 ADC 多工器選擇，所以多通道會分時，不是同步取樣。[Raspberry Pi hardware APIs](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_adc)

RP2040 有 264 KiB SRAM。240×320 RGB565 全畫面緩衝需要 153,600 bytes，也就是 150 KiB，會留下約 114 KiB 給其他 SRAM 資料、堆疊與 SDK。ST7789 控制器有自己的畫面記憶體；韌體應畫網格一次，再更新波形區或短列，不要先做全畫面雙緩衝。[RP2040 規格](https://www.raspberrypi.com/documentation/microcontrollers/microcontroller-chips.html)、[ST7789V 資料表](https://newhavendisplay.com/content/datasheets/ST7789V.pdf)

## 韌體分層

| 模組 | 職責 |
|---|---|
| board_config | GPIO、SPI bus、LCD reset/backlight、按鍵、ADC 腳位；依實際 LCD 模組與載板確定 |
| adc_capture | ADC FIFO、DREQ、DMA、擷取時間限制、FIFO 溢位檢查 |
| trigger | 上升／下降緣閾值搜尋、觸發索引、AUTO 無觸發退路 |
| calibration | ADC code 到輸入電壓的比例和零點；依 AFE 與校正結果設定 |
| measurements | Vpp、平均值、RMS、頻率；無足夠週期時回報無效，而非假值 |
| st7789 | 面板初始化、方向、顏色次序、設定區域與 RGB565 burst |
| scope_view | 網格、狀態列、量程、時基、讀數和波形像素降採樣 |
| protocol | COBS、封包長度、CRC、版本和訊息處理 |
| main | 開機、待機、擷取、處理、繪圖、傳輸狀態機 |

建議的狀態順序：

BOOT → IDLE → ARM → CAPTURE → PROCESS → DRAW → IDLE  
　　　　　　　　　　　　　　　　　　　　　　└→ USB SEND（已連線時）

USB 斷線只關閉傳輸，不停止本機顯示。錯誤狀態至少要區分 ADC FIFO 溢位、無觸發、封包錯誤、設定超界和 LCD 未初始化。

## ADC + DMA 擷取

官方 pico-examples/adc/dma_capture 範例示範讓 ADC 在 free-running 模式取樣，再由 DMA 以 DREQ_ADC 從 FIFO 搬到 RAM。[官方 ADC DMA 範例](https://github.com/raspberrypi/pico-examples/blob/master/adc/dma_capture/dma_capture.c)

建議的單次擷取順序：

1. 呼叫 **adc_init()**，初始化 ADC0 對應的 **GPIO26**，選擇 ADC input 0。
2. 設定 ADC FIFO、DREQ 閾值、錯誤旗標和取樣分頻。
3. 清除舊 FIFO，再設定 DMA：16-bit transfer、固定讀取 **adc_hw->fifo**、RAM 位址遞增、DREQ 為 **DREQ_ADC**。
4. 啟動 DMA 和 ADC，等待 DMA 完成；等待必須有 timeout。
5. 停止 ADC、清理 FIFO，檢查 FIFO 錯誤，再交給觸發與顯示程式。
6. 對設定的 sample rate 做實際計時驗證；不要用 CPU 迴圈輪詢 ADC 取代硬體擷取。

單通道 v1 可先預留兩個 1,024-sample **uint16_t** buffer；雙緩衝約用 4 KiB SRAM。240 個顯示欄位可用每欄的樣本最小值與最大值繪圖，避免單純平均掉窄脈衝。1,024 是軟體起始值，不代表已量過的最佳設定。

## ST7789 與 240×320 LCD

ST7789V 控制器資料表列出 240RGB×320；但 LCD 模組的排線、電源、背光、CS 腳、旋轉方向、顏色次序和 row/column offset 由模組實作決定。先拿到完整模組料號和資料表，再凍結 PCB 腳位與初始化表。

初始化流程依面板資料表建立，通常包含 reset、sleep out、pixel format、MADCTL／方向、顯示區域、inversion 設定和 display on。資料傳輸使用 **CASET (0x2A)**、**RASET (0x2B)** 指定範圍，再用 **RAMWR (0x2C)** 寫 RGB565 像素。先用低更新量的色條、座標和網格測試方向與顏色，再加入波形。

Raspberry Pi 官方 pio_st7789_lcd 範例可當初始化和 PIO 傳輸參考；範例以 PIO 做高速連續畫面，不需要整份搬進低更新量示波器。v1 先用硬體 SPI 寫局部區域；如果實測更新不足，再評估 SPI DMA 或 PIO。[官方 ST7789 範例](https://github.com/raspberrypi/pico-examples/tree/master/pio/st7789_lcd)

## 觸發、讀數和顯示

- ADC 原始碼先保留在擷取 buffer。以 AFE 校正係數換算電壓，不在擷取中做浮點運算。
- 在資料區搜尋上升緣條件「previous < level 且 current >= level」，或下降緣的反向條件；必要時加入 hysteresis。
- 以觸發點作為畫面中心附近的基準，擷取前後樣本。若未找到觸發，AUTO 模式顯示最新資料並標示未觸發。
- 頻率讀數先以多個同向 crossing 的時間差計算；不足兩個有效 crossing 時顯示「—」。
- 波形降採樣到繪圖寬度時保留每個 pixel bucket 的 min/max，避免窄峰消失。
- 重畫時先清波形區背景，再畫波形；固定網格與標籤不必每次整屏重畫。
- Vpp、RMS、頻率都要標明校正是否有效；沒有校正證據時畫面只展示 ADC code 或標示為估算值。

## USB CDC 與瀏覽器

O-SCOPE 網頁用 Web Serial、JSON 設定和文字樣本；這對示範方便，但文字 CSV 傳輸多佔位，也難以檢查資料完整性。[O-SCOPE 網頁原始碼](https://github.com/mandrecut/o_scope/blob/fc3c95a8090a39670ce01c78a890f21adcd6cfaf/assets/index.min.js)

本專案建議用 [USB 通訊協定草案](PROTOCOL_V1.md)。韌體使用 TinyUSB CDC byte API；USB CDC 保持為純封包通道，除錯訊息走 UART 或 SWD。瀏覽器在使用者按下 Connect 時呼叫 **navigator.serial.requestPort()**，再開啟 **port.readable** 和 **port.writable**。Web Serial 需要安全來源；GitHub Pages 提供靜態 HTML/CSS/JavaScript，使用 HTTPS 網址即可，不需要 Pico 自己開 web server。[Chrome Web Serial 文件](https://developer.chrome.com/docs/capabilities/serial)、[Web Serial 標準](https://wicg.github.io/serial/)、[GitHub Pages 文件](https://docs.github.com/en/pages/getting-started-with-github-pages/what-is-github-pages)

前端讀取器要把多次 **reader.read()** 收到的任意分段合併後再解封包，不能假設一次 read 就是一個完整封包。UI 至少要呈現未支援 Web Serial、未授權、連線中、已連線、裝置拔除、CRC 錯誤和擷取逾時。

## PlatformIO 程式目錄

    platformio.ini
    src/
      main.cpp
      capture/adc_capture.cpp
      capture/trigger.cpp
      display/st7789.cpp
      display/scope_view.cpp
      measurements.cpp
      protocol/frame_codec.cpp
    include/
      board_config.h
      scope_config.h
    lib/
      # 專案自有小型模組；Pico SDK APIs 由 picosdk framework 提供
    web/
      index.html
      css/scope.css
      js/app.js
      js/serial.js
      js/protocol.js
      js/plot.js
    hardware/
      rev-a/
    tests/
      host/
    docs/

韌體由根目錄 `platformio.ini` 管理，PlatformIO 的 `src/`、`include/`、`lib/` 分別放韌體入口、共用設定／標頭及專案自有模組。開始功能開發前，先新增最小 `src/main.cpp`，執行 `pio run -e pico` 產生 UF2，再把第一次成功建置所解析的平台與 SDK 版本固定下來。此專案目前尚無程式，因此這一步仍未執行。網頁用 ES modules 和 Canvas，依相對路徑載入檔案，讓它能放在 GitHub Pages 的 /repository/ 子路徑。先不加入前端 framework 或即時後端服務。

## 類比前端與自製 PCB

不要把探棒直接接到 RP2040 ADC 腳。AFE 要將輸入衰減／偏壓到 ADC 安全範圍，並提供輸入限流保護、緩衝和抗混疊濾波；元件值要等到輸入範圍、探棒阻抗和目標頻寬確定後才設計。這個 v1 僅定位為低電壓教學／除錯工具，不量市電。

Rev A 建議先做 Pico 載板，不先整合裸 RP2040。保留 Pico USB、SWD／BOOTSEL、測試點；將 ADC 前端放近輸入接頭，ST7789 接頭帶出 SPI、DC、RESET、背光和模組需要的 CS。LCD 模組和 ADC 腳分開標記，避免原理圖把 UI 假設誤當成面板合約。

## 開發與驗收階段

| 階段 | 工作 | 驗收證據 |
|---|---|---|
| 0 | 建 PlatformIO 最小韌體與鎖定 SDK／平台版本 | `pio run -e pico` 成功編出 Pico UF2；記錄實際解析到的平台和 SDK commit |
| 1 | ST7789 初始化、RGB565 色條、網格和方向 | 實際 LCD 逐項核對模組資料表 |
| 2 | ADC FIFO + DMA 單次擷取 | 低電壓函數產生器輸入；確認樣本數、間隔和 FIFO 狀態 |
| 3 | 觸發、校正、Vpp/RMS/頻率 | 比對已知低電壓訊號並記錄誤差 |
| 4 | COBS/CRC 協定主機測試 | 測試分段、黏包、壞 CRC、超長封包和 sequence |
| 5 | Web Serial + Canvas | HTTPS 頁面連接、操作、斷線和重連可見 |
| 6 | Rev A 載板整合 | 先做低電壓功能測試，再做連續運作與熱／雜訊檢查 |

上述是待執行驗收，不是本次已通過的測試。此刻已完成 source-level 研究、PlatformIO 工具選型與 Git 初始化；尚未建立韌體、網頁程式、原理圖或硬體驗收結果。

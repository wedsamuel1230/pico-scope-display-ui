# Pico 示波器與 ST7789 參考專案比較

檢視日期：2026-10-17。以下專案用來比較架構和技術選擇；除非本文件明確提到，不代表本專案已複製、建置或實測其程式。要複用程式碼前，需另外核對該 repository 的 license、硬體版本和相依套件。

| 專案 | 相似之處 | 可借鑑 | 不宜直接照搬 |
|---|---|---|---|
| [O-SCOPE](https://github.com/mandrecut/o_scope) | Pico 示波器、ADC/DMA、瀏覽器 Web Serial、GitHub Pages；和本專案的「本機 LCD + 網頁」混合方向最接近。 | Web Serial 連線流程、控制與資料 UI、UF2 發佈方式，以及論文對 ADC/PWM/類比前端的架構說明。 | 檢視的 GitHub tree 有網頁和 RP2040/RP2350 UF2，沒有可修改的韌體原始碼；論文韌體是 MicroPython。2.5 MS/s 涉及超頻，不能當成 RP2040 v1 保證。O-SCOPE 網頁功能量較多，應重排成 240×320 LCD 操作層級。 |
| [pico-RP2040-oscilloscope](https://github.com/jklomp/pico-RP2040-oscilloscope) | C + Pico SDK，USB serial 串接 HTML/JavaScript Web Serial UI；是最接近本專案 USB 連線路徑的另一個參考。 | ADC 擷取到瀏覽器的最小 end-to-end 資料流、基本觸發控制與 Pico C SDK 專案思路。 | 沒有內建 ST7789 本機顯示；README 指出低頻定位和 GP26/GP27 0–3.3 V 量測。repo 把 Pico 程式打包在 ZIP，需先檢查原始碼結構和授權再挑可用部分。 |
| [picOOscilloscope](https://github.com/AgainPsychoX/picOOscilloscope) | Pico + PlatformIO + SPI TFT + 本機示波器 UI；它是 PlatformIO 設定和畫面模組分工的實例。 | `platformio.ini`、測試 target、SPI pin 分配，以及顯示/觸控同 bus 的資源安排。 | 使用 4 吋 480×320 ST7796 + XPT2046，非 2.4 吋 240×320 ST7789；目前標示 work in progress。PIO 使用 Arduino-Pico (`framework = arduino`, Earle Philhower core)，不是原生 `framework = picosdk`。 |
| [Raspberry-Pi-Pico-PIO-LCD-Driver](https://github.com/stienman/Raspberry-Pi-Pico-PIO-LCD-Driver) | 原生 Pico 程式碼、ST7789 SPI LCD、接近 240×320 的畫素維度（320×240 landscape）。 | LCD address window、局部矩形寫入、RGB565，以及在需要高吞吐時以 PIO + DMA 輸送畫面。專案標示 BSD-2-Clause。 | 驅動器會使用 6 個 DMA channel 並完全佔用 PIO0，對也要使用 ADC DMA 的 scope 韌體來說成本很高。v1 先用 hardware SPI，確認畫面更新瓶頸後再取用部分優化。 |
| [Hadosoku](https://github.com/martinbogo/hadosoku) | Pico W 示波器、DMA 擷取、trigger、波形 UI、瀏覽器控制。 | `?demo` 模式用模擬裝置輸出相同 binary frame，適合作為本專案未接板時開發/展示 UI 的參考；也值得參考 aliasing 和 capture blind spot 的使用者提示。 | 韌體用 MicroPython，網頁走 Pico W Wi-Fi HTTP/WebSocket，不是 USB Web Serial。README 把輸入限制列為 0–3.3 V 且沒有板上輸入保護，並列出無認證/加密；不要把網路服務或輸入電路當成可直接沿用設計。 |

## 對本專案的整合決策

1. **架構**：借 O-SCOPE 與 jklomp 的 browser bridge 概念，但本機波形和操作必須由 Pico + ST7789 獨立完成，瀏覽器連線屬可選增強。
2. **韌體**：新程式以 PlatformIO 的原生 `picosdk` framework 為目標；`picOOscilloscope` 只作 PlatformIO/模組分工參考，不混入 Arduino framework。
3. **LCD**：先用硬體 SPI 和局部更新。ST7789 PIO driver 作為之後效能 profiling 的參照，保留 ADC DMA 和 PIO 資源餘裕。
4. **網站**：建立可純瀏覽器執行的 demo transport，仿照 Hadosoku 的 deterministic simulator 思路，使沒有 Pico 時也能開發 Canvas、protocol parser 和互動狀態。
5. **量測界限**：參考專案宣稱的取樣率或性能不是本機設計的測量結果。最終規格要以實際 PlatformIO build、低電壓訊號源、已知負載和校正證據確立。

# USB CDC 通訊協定草案 v1

狀態：建議草案，尚未實作或以實體 Pico 驗證。目標是讓韌體和 Web Serial 使用一套可恢復、可版本化的 binary byte stream。

## Frame 格式

一個封包以 COBS 編碼後送出，並以 0x00 作結尾：

    COBS(version | type | sequence | payload_length | payload | crc16) | 0x00

所有整數使用 little-endian。

| 欄位 | 大小 | 意義 |
|---|---:|---|
| version | 1 byte | 協定版本，目前為 1 |
| type | 1 byte | 訊息種類 |
| sequence | 2 bytes | 每次請求遞增；回覆帶回同一序號 |
| payload_length | 2 bytes | 解碼後 payload 長度 |
| payload | 0–N bytes | 訊息資料 |
| crc16 | 2 bytes | CRC-16/CCITT-FALSE，覆蓋未編碼的 header 與 payload |

COBS 讓 0x00 delimiter 不會出現在編碼 frame 內。Web Serial 的每次 read() 可能只收到 frame 的一部分，也可能一次收到多個 frame；兩端都必須累積 bytes 到 delimiter，再解碼和檢查。

## v1 訊息

| Type | 方向 | Payload |
|---|---|---|
| HELLO_REQ | Browser → Pico | 空 |
| HELLO_RSP | Pico → Browser | 韌體版本、能力旗標、最大樣本數、輸入量程／校正狀態 |
| SET_CONFIG | Browser → Pico | requested sample rate u32、sample count u16、trigger level ADC code u16、edge u8、mode u8 |
| ACK | Pico → Browser | 對應 request sequence、狀態碼、實際採用的設定 |
| CAPTURE_REQ | Browser → Pico | 空；本機按鍵也可觸發相同擷取 |
| CAPTURE_DATA | Pico → Browser | 實際 sample rate u32、sample count u16、trigger index u16（0xFFFF 表示無觸發）、status flags u8、ADC raw samples u16[] |
| STOP | Browser → Pico | 空 |
| ERROR | Pico → Browser | 對應 sequence、錯誤碼、短錯誤細節 |

CAPTURE_DATA 的 raw samples 使用 16-bit little-endian 容器；RP2040 ADC 有效資料為 12-bit。輸入電壓換算係數和量程必須等 AFE 設計及校正後再固定進 HELLO_RSP 或資料封包。

## 封包解析與安全界線

- 韌體 parser 使用固定大小 buffer；超長 frame 直接丟棄，直到下一個 0x00。
- 先檢查 COBS、版本、長度和 CRC，再解析 payload。
- sample_count 不可超過韌體配置的最大 buffer；sample_rate_hz 必須落在韌體明確支援的範圍。
- 未知 type、設定超界或錯誤版本要回覆 ERROR，不可直接寫入硬體設定。
- 擷取資料包回報實際採用的 sample rate，不只回報瀏覽器請求值。
- ADC 溢位、無觸發、校正未完成和截斷資料用 status flags 表示。
- 同一 CDC byte stream 不輸出 printf、ASCII banner 或 CSV。

## Web Serial 生命週期

1. 使用者點擊 Connect 時呼叫 navigator.serial.requestPort()。
2. 取得權限後呼叫 port.open({ baudRate: ... })。baudRate 是串列設定欄位，不是 ADC sample rate。
3. 透過 port.readable.getReader() 持續讀 bytes，交給 COBS decoder。
4. 透過 port.writable.getWriter() 傳送已編碼的命令；關閉時取消 reader、釋放 reader/writer lock，再關閉 port。
5. 監聽 disconnect；裝置拔除後清除連線 UI 狀態，保留最後一個有效波形或顯示 disconnected。
6. 在 GitHub Pages HTTPS 網址執行；Web Serial 要求安全來源，而且 port prompt 必須由使用者動作觸發。

Chrome 的 Web Serial API 文件說明 readable/writable 是以 Uint8Array 傳輸的 streams，並要求妥善釋放 reader/writer lock。[Chrome Web Serial 文件](https://developer.chrome.com/docs/capabilities/serial)、[Web Serial 安全來源與 requestPort 規範](https://wicg.github.io/serial/)

## 尚待確認

- 最終最大 sample count 和最大封包長度。
- AFE 固定量程或可切換量程，以及校正係數的 wire representation。
- 命令佇列深度與 Pico 本機按鍵操作和 browser 設定的優先順序。
- USB CDC implementation 選用的 TinyUSB/Pico SDK integration 和 nominal baud rate。

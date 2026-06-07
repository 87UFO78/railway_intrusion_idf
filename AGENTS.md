# AGENTS.md

## Project Overview

這是 ESP-IDF 鐵路異物入侵偵測系統。

系統流程：
1. ESP32-S3 連接 Wi-Fi
2. PC App 連線並完成時間同步
3. IR sensor 偵測異物
4. 偵測到後 camera 拍照
5. 圖片存到 SD card
6. PC App 透過 HTTP endpoint 取得警報紀錄與圖片
7. 使用者判斷是否為異物
8. 根據結果控制警報與系統狀態

## Important Modules

- main.c：系統初始化與 task 啟動
- camera_app.c：camera 初始化、拍照、frame buffer 管理
- server_app.c：HTTP server、stream、API endpoint
- ir_app.c：IR sensor 偵測
- alarm_app.c：警報紀錄管理
- sd_app.c：SD card 初始化與圖片儲存
- system_state.c：全域狀態管理

## Important Endpoints

不要任意更改以下 endpoint 名稱與回傳格式：

- /test
- /stream
- /set_time
- /last_alarm
- /all_alarms
- /photo
- /update_alarm
- /delete_alarm
- /reset_alarm

## System Rules

- PC 未連線時，不可啟動 IR detection
- 尚未完成 time sync 時，不可啟動 IR detection
- 偵測到異物後，IR detection 必須暫停
- 使用者選擇「不是異物」後，系統才恢復偵測
- 使用者選擇「判斷為異物」後，警報保持
- 使用者選擇「稍後處理」後，警報保持
- PC 斷線時，buzzer 必須停止
- camera frame buffer 使用後必須釋放

## Coding Rules

- 不要一次大幅重構多個模組
- 修改前先說明計畫
- 修改後列出變更檔案與主要差異
- 不要移除現有 log，除非明確要求
- 不要改變現有 HTTP API 格式，除非明確要求
- 修改 camera 相關程式時，要特別注意 frame buffer release
- 修改 server 相關程式時，要特別注意 client disconnect handling

## Response Style

請使用繁體中文回答。
程式碼註解也使用繁體中文。
如果不確定硬體行為，請明確說明需要實測確認。
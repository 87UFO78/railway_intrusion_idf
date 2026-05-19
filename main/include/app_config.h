#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define DEBUG_MODE 0   // 1 = 顯示除錯訊息，0 = 關閉除錯訊息

#if DEBUG_MODE
#define DEBUG_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#define DEBUG_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#define DEBUG_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#else
#define DEBUG_LOGI(tag, fmt, ...)
#define DEBUG_LOGW(tag, fmt, ...)
#define DEBUG_LOGE(tag, fmt, ...)
#endif

#endif
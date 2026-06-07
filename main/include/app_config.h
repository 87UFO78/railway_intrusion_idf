#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define DEBUG_MODE 0

#define APP_CORE_NETWORK 0
#define APP_CORE_CAMERA  1

#define APP_TASK_PRIORITY_BACKGROUND 3
#define APP_TASK_PRIORITY_CONTROL    5
#define APP_TASK_PRIORITY_CAMERA     6

#if DEBUG_MODE
#define DEBUG_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#define DEBUG_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#define DEBUG_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#else
#define DEBUG_LOGI(tag, fmt, ...) do { (void)(tag); } while (0)
#define DEBUG_LOGW(tag, fmt, ...) do { (void)(tag); } while (0)
#define DEBUG_LOGE(tag, fmt, ...) do { (void)(tag); } while (0)
#endif

#endif

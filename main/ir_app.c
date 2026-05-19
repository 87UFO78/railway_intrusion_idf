#include "ir_app.h"
#include "app_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "system_state.h"
#include "camera_app.h"
#include "sd_app.h"
#include "alarm_app.h"

#define IR_PIN GPIO_NUM_14

static const char *TAG = "ir_app";

static TaskHandle_t camera_task_handle = NULL;

static void camera_task(void *pvParameters)
{
    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!g_systemReady || !g_pcOnline)
        {
            DEBUG_LOGW(TAG, "系統未準備好，不拍照");
            continue;
        }

        camera_fb_t *fb = camera_app_capture();
        uint32_t image_id = 0;

        if (fb)
        {
            image_id = ++g_imageCounter;

            if (sd_app_save_jpg_by_id(image_id, fb))
            {
                DEBUG_LOGI(TAG, "異物照片儲存成功 ImageId=%lu size=%d",
                         (unsigned long)image_id,
                         fb->len);
            }
            else
            {
                DEBUG_LOGE(TAG, "異物照片儲存失敗");
                image_id = 0;
            }

            camera_app_return(fb);
        }
        else
        {
            DEBUG_LOGE(TAG, "異物照片拍攝失敗，仍然建立警報紀錄");
        }

        char time_str[32] = {0};
        uint32_t alarm_id = ++g_alarmCounter;
        alarm_app_add_record(alarm_id, image_id, "未判斷", time_str);
    }
}

static void detection_task(void *pvParameters)
{
    int last_state = 1;

    while (!g_systemReady)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    while (1)
    {
        int val = gpio_get_level(IR_PIN);

        if (!g_systemReady || !g_pcOnline)
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (system_millis() < g_resetCooldownUntilMs)
        {
            last_state = val;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (g_waitIrRelease)
        {
            if (val == 1)
            {
                DEBUG_LOGI(TAG, "IR 已釋放，恢復偵測");
                g_waitIrRelease = false;
                g_allowDetect = true;
                last_state = 1;
            }

            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (g_alarmActive || !g_allowDetect)
        {
            last_state = val;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (val == 0 && last_state == 1)
        {
            DEBUG_LOGW(TAG, "偵測到異物");

            system_set_alarm_active(true);
            g_allowDetect = false;

            if (camera_task_handle != NULL)
            {
                xTaskNotifyGive(camera_task_handle);
            }
        }

        last_state = val;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void ir_app_start(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << IR_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));

    xTaskCreatePinnedToCore(
        camera_task,
        "camera_task",
        8192,
        NULL,
        5,
        &camera_task_handle,
        1
    );

    xTaskCreatePinnedToCore(
        detection_task,
        "detection_task",
        4096,
        NULL,
        5,
        NULL,
        1
    );

    DEBUG_LOGI(TAG, "IR task started, pin=%d", IR_PIN);
}

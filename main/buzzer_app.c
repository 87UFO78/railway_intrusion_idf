#include "buzzer_app.h"
#include "app_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "system_state.h"

#define BUZZER_PIN GPIO_NUM_3

static const char *TAG = "buzzer_app";

static void buzzer_task(void *pvParameters)
{
    while (1)
    {
        if (g_alarmActive && g_pcOnline)
        {
            gpio_set_level(BUZZER_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(120));
            gpio_set_level(BUZZER_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(880));
        }
        else
        {
            gpio_set_level(BUZZER_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

void buzzer_app_start(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUZZER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(BUZZER_PIN, 0);

    xTaskCreatePinnedToCore(
        buzzer_task,
        "buzzer_task",
        4096,
        NULL,
        APP_TASK_PRIORITY_BACKGROUND,
        NULL,
        APP_CORE_NETWORK
    );

    DEBUG_LOGI(TAG, "Buzzer task started, pin=%d", BUZZER_PIN);
}

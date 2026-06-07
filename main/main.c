#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "nvs_flash.h"
#include "esp_log.h"

#include "system_state.h"
#include "wifi_app.h"
#include "camera_app.h"
#include "sd_app.h"
#include "alarm_app.h"
#include "server_app.h"
#include "ir_app.h"
#include "buzzer_app.h"
#include "app_config.h"

static const char *TAG = "main";

void app_main(void)
{
    DEBUG_LOGI(TAG, "Railway intrusion system starting...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    setenv("TZ", "CST-8", 1);
    tzset();

    system_state_init();

    wifi_app_start();

    esp_err_t camera_ret = camera_app_init();
    if (camera_ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera unavailable: %s", esp_err_to_name(camera_ret));
    }

    sd_app_init();

    alarm_app_init();
    alarm_app_load_last_index();

    server_app_start();
    ir_app_start();
    buzzer_app_start();

    DEBUG_LOGI(TAG, "System init done");
}

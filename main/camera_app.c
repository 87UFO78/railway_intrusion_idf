#include "camera_app.h"
#include "app_config.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "camera_app";

#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5

#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM       8
#define Y3_GPIO_NUM       9
#define Y2_GPIO_NUM       11

#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM     13

static SemaphoreHandle_t camera_mutex = NULL;

esp_err_t camera_app_init(void)
{
    camera_mutex = xSemaphoreCreateMutex();
    if (camera_mutex == NULL)
    {
        DEBUG_LOGE(TAG, "Camera mutex create failed");
        return ESP_FAIL;
    }

    camera_config_t config = {
        .pin_pwdn = PWDN_GPIO_NUM,
        .pin_reset = RESET_GPIO_NUM,
        .pin_xclk = XCLK_GPIO_NUM,
        .pin_sccb_sda = SIOD_GPIO_NUM,
        .pin_sccb_scl = SIOC_GPIO_NUM,
        .pin_d7 = Y9_GPIO_NUM,
        .pin_d6 = Y8_GPIO_NUM,
        .pin_d5 = Y7_GPIO_NUM,
        .pin_d4 = Y6_GPIO_NUM,
        .pin_d3 = Y5_GPIO_NUM,
        .pin_d2 = Y4_GPIO_NUM,
        .pin_d1 = Y3_GPIO_NUM,
        .pin_d0 = Y2_GPIO_NUM,
        .pin_vsync = VSYNC_GPIO_NUM,
        .pin_href = HREF_GPIO_NUM,
        .pin_pclk = PCLK_GPIO_NUM,
        .xclk_freq_hz = 10000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 8,
        .fb_count = 2,
        .grab_mode = CAMERA_GRAB_LATEST,
        .fb_location = CAMERA_FB_IN_PSRAM
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        DEBUG_LOGE(TAG, "Camera init failed: 0x%x", err);
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s)
    {
        s->set_vflip(s, 1);
        s->set_brightness(s, 1);
        s->set_contrast(s, 1);
        s->set_saturation(s, 1);
    }

    for (int i = 0; i < 3; i++)
    {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb)
        {
            DEBUG_LOGI(TAG, "Camera warmup frame %d OK, size=%d", i + 1, fb->len);
            esp_camera_fb_return(fb);
        }
        else
        {
            DEBUG_LOGW(TAG, "Camera warmup frame %d failed", i + 1);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    DEBUG_LOGI(TAG, "Camera init success");
    return ESP_OK;
}

camera_fb_t *camera_app_capture(void)
{
    if (camera_mutex == NULL)
    {
        DEBUG_LOGE(TAG, "Camera mutex not ready");
        return NULL;
    }

    if (xSemaphoreTake(camera_mutex, pdMS_TO_TICKS(3000)) != pdTRUE)
    {
        DEBUG_LOGE(TAG, "Camera mutex timeout");
        return NULL;
    }

    camera_fb_t *fb = NULL;

    for (int i = 0; i < 5; i++)
    {
        fb = esp_camera_fb_get();
        if (fb)
        {
            DEBUG_LOGI(TAG, "Camera capture OK, size=%d", fb->len);
            return fb;
        }

        DEBUG_LOGW(TAG, "Camera capture failed, retry %d", i + 1);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    xSemaphoreGive(camera_mutex);
    DEBUG_LOGE(TAG, "Camera capture failed after retries");
    return NULL;
}

void camera_app_return(camera_fb_t *fb)
{
    if (fb)
    {
        esp_camera_fb_return(fb);
    }

    if (camera_mutex)
    {
        xSemaphoreGive(camera_mutex);
    }
}

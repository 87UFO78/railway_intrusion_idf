#include "server_app.h"
#include "app_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <time.h>

#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system_state.h"
#include "camera_app.h"
#include "alarm_app.h"
#include "sd_app.h"

static const char *TAG = "server_app";
static httpd_handle_t api_server = NULL;
static httpd_handle_t stream_server = NULL;

#define PART_BOUNDARY "frame"
#define STREAM_BUFFER_GROW_SIZE (16 * 1024)
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace; boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t send_service_unavailable(httpd_req_t *req, const char *message)
{
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
}

static bool copy_camera_frame(uint8_t **buffer, size_t *capacity, size_t *frame_size)
{
    camera_fb_t *fb = camera_app_capture();
    if (!fb)
    {
        return false;
    }

    if (fb->format != PIXFORMAT_JPEG)
    {
        DEBUG_LOGE(TAG, "Camera frame is not JPEG");
        camera_app_return(fb);
        return false;
    }

    if (fb->len > *capacity)
    {
        size_t new_capacity =
            ((fb->len + STREAM_BUFFER_GROW_SIZE - 1) / STREAM_BUFFER_GROW_SIZE) *
            STREAM_BUFFER_GROW_SIZE;
        uint8_t *new_buffer = heap_caps_realloc(
            *buffer,
            new_capacity,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );

        if (!new_buffer)
        {
            new_buffer = heap_caps_realloc(*buffer, new_capacity, MALLOC_CAP_8BIT);
        }

        if (!new_buffer)
        {
            ESP_LOGE(TAG, "Failed to allocate frame buffer, size=%u", (unsigned)new_capacity);
            camera_app_return(fb);
            return false;
        }

        *buffer = new_buffer;
        *capacity = new_capacity;
    }

    memcpy(*buffer, fb->buf, fb->len);
    *frame_size = fb->len;
    camera_app_return(fb);
    return true;
}

static void url_decode(char *dst, const char *src, size_t dst_size)
{
    char a, b;
    size_t i = 0;

    while (*src && i < dst_size - 1)
    {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) &&
            isxdigit((unsigned char)a) && isxdigit((unsigned char)b))
        {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a = a - 'A' + 10;
            else a -= '0';

            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b = b - 'A' + 10;
            else b -= '0';

            dst[i++] = (char)(16 * a + b);
            src += 3;
        }
        else if (*src == '+')
        {
            dst[i++] = ' ';
            src++;
        }
        else
        {
            dst[i++] = *src++;
        }
    }

    dst[i] = '\0';
}

static esp_err_t read_req_body(httpd_req_t *req, char *buffer, size_t buffer_size)
{
    int total_len = req->content_len;
    int cur_len = 0;

    if (total_len <= 0 || total_len >= (int)buffer_size)
    {
        return ESP_FAIL;
    }

    while (cur_len < total_len)
    {
        int received = httpd_req_recv(req, buffer + cur_len, total_len - cur_len);
        if (received <= 0)
        {
            return ESP_FAIL;
        }
        cur_len += received;
    }

    buffer[cur_len] = '\0';
    return ESP_OK;
}

static esp_err_t test_handler(httpd_req_t *req)
{
    g_pcConnected = true;
    system_touch_pc_alive();
    system_state_update();

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t disconnect_handler(httpd_req_t *req)
{
    system_mark_pc_offline();

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "DISCONNECTED", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t set_time_handler(httpd_req_t *req)
{
    system_touch_pc_alive();

    char query[64];
    char ts_str[32];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ts");
        return ESP_FAIL;
    }

    if (httpd_query_key_value(query, "ts", ts_str, sizeof(ts_str)) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ts");
        return ESP_FAIL;
    }

    time_t timestamp = strtoul(ts_str, NULL, 10);
    struct timeval now = {
        .tv_sec = timestamp,
        .tv_usec = 0
    };

    settimeofday(&now, NULL);

    g_timeSynced = true;
    system_set_alarm_active(false);
    g_allowDetect = true;
    system_state_update();

    DEBUG_LOGI(TAG, "對時成功");

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "TIME_OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    system_touch_pc_alive();

    char response[192];
    snprintf(response, sizeof(response),
             "{\"pcConnected\":%s,\"timeSynced\":%s,\"systemReady\":%s,\"pcOnline\":%s,"
             "\"alarmActive\":%s,\"cameraReady\":%s}",
             g_pcConnected ? "true" : "false",
             g_timeSynced ? "true" : "false",
             g_systemReady ? "true" : "false",
             g_pcOnline ? "true" : "false",
             g_alarmActive ? "true" : "false",
             camera_app_is_ready() ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t last_alarm_handler(httpd_req_t *req)
{
    system_touch_pc_alive();

    char response[256];

    if (!alarm_app_get_last_alarm_json(response, sizeof(response), true))
    {
        httpd_resp_set_status(req, "204 No Content");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t all_alarms_handler(httpd_req_t *req)
{
    system_touch_pc_alive();

    char *content = NULL;
    size_t size = 0;

    if (!sd_app_read_alarms(&content, &size))
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "NO_ALARMS");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, content, size);
    free(content);
    return ESP_OK;
}

static esp_err_t photo_handler(httpd_req_t *req)
{
    system_touch_pc_alive();

    char query[128];
    char id_str[32];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing id");
        return ESP_FAIL;
    }

    if (httpd_query_key_value(query, "id", id_str, sizeof(id_str)) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing id");
        return ESP_FAIL;
    }

    uint32_t image_id = strtoul(id_str, NULL, 10);
    uint8_t *buffer = NULL;
    size_t size = 0;

    if (!sd_app_read_photo_by_id(image_id, &buffer, &size))
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "file not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_send(req, (const char *)buffer, size);
    free(buffer);
    return ESP_OK;
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    system_touch_pc_alive();

    if (!camera_app_is_ready())
    {
        return send_service_unavailable(req, "Camera unavailable");
    }

    uint8_t *frame_buffer = NULL;
    size_t frame_capacity = 0;
    size_t frame_size = 0;

    if (!copy_camera_frame(&frame_buffer, &frame_capacity, &frame_size))
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera capture failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    esp_err_t ret = httpd_resp_send(req, (const char *)frame_buffer, frame_size);
    heap_caps_free(frame_buffer);
    return ret;
}

static esp_err_t update_alarm_handler(httpd_req_t *req)
{
    system_touch_pc_alive();

    char body[512];
    if (read_req_body(req, body, sizeof(body)) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
        return ESP_FAIL;
    }

    if (!alarm_app_update_from_json(body))
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "ImageId Not Found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t delete_alarm_handler(httpd_req_t *req)
{
    system_touch_pc_alive();

    char body[512];
    if (read_req_body(req, body, sizeof(body)) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
        return ESP_FAIL;
    }

    bool deleted = alarm_app_delete_from_json(body);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, deleted ? "DELETED" : "NOT_FOUND", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t reset_alarm_handler(httpd_req_t *req)
{
    system_touch_pc_alive();

    system_set_alarm_active(false);
    g_allowDetect = false;
    g_waitIrRelease = true;
    g_resetCooldownUntilMs = system_millis() + 1500;
    g_hasNewAlarm = false;

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "RESET_OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t update_alarm_get_handler(httpd_req_t *req)
{
    system_touch_pc_alive();

    char query[256];
    char id_str[32];
    char result_encoded[128];
    char result[64];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query");
        return ESP_FAIL;
    }

    if (httpd_query_key_value(query, "id", id_str, sizeof(id_str)) != ESP_OK ||
        httpd_query_key_value(query, "result", result_encoded, sizeof(result_encoded)) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing id/result");
        return ESP_FAIL;
    }

    url_decode(result, result_encoded, sizeof(result));

    char body[256];
    snprintf(body, sizeof(body),
             "{\"AlarmId\":%lu,\"ImageId\":%lu,\"Result\":\"%s\"}",
             (unsigned long)strtoul(id_str, NULL, 10),
             (unsigned long)strtoul(id_str, NULL, 10),
             result);

    if (!alarm_app_update_from_json(body))
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Alarm not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"updated\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    system_touch_pc_alive();
    uint32_t stream_generation = system_connection_generation();

    if (!camera_app_is_ready())
    {
        return send_service_unavailable(req, "Camera unavailable");
    }

    char part_buf[96];
    uint8_t *frame_buffer = NULL;
    size_t frame_capacity = 0;
    size_t frame_size = 0;
    esp_err_t ret = ESP_OK;
    bool client_disconnected = false;

    if (!copy_camera_frame(&frame_buffer, &frame_capacity, &frame_size))
    {
        heap_caps_free(frame_buffer);
        return send_service_unavailable(req, "Camera capture failed");
    }

    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    while (true)
    {
        system_touch_pc_alive();

        size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, (unsigned)frame_size);

        ret = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (ret != ESP_OK)
        {
            client_disconnected = true;
            break;
        }

        ret = httpd_resp_send_chunk(req, part_buf, hlen);
        if (ret != ESP_OK)
        {
            client_disconnected = true;
            break;
        }

        ret = httpd_resp_send_chunk(req, (const char *)frame_buffer, frame_size);
        if (ret != ESP_OK)
        {
            client_disconnected = true;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(30));

        if (!copy_camera_frame(&frame_buffer, &frame_capacity, &frame_size))
        {
            ESP_LOGE(TAG, "Stream stopped: camera capture failed");
            ret = ESP_FAIL;
            break;
        }
    }

    heap_caps_free(frame_buffer);
    if (client_disconnected && system_connection_is_active(stream_generation))
    {
        system_mark_pc_offline();
    }
    DEBUG_LOGI(TAG, "stream: client disconnected");
    return ret;
}

static void pc_watchdog_task(void *pvParameters)
{
    const uint32_t TIMEOUT_MS = 2500;

    while (1)
    {
        if (g_pcOnline)
        {
            uint32_t now = system_millis();
            if (now - g_pcLastSeenMs > TIMEOUT_MS)
            {
                system_mark_pc_offline();
                DEBUG_LOGW(TAG, "PC 斷線");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void register_uri(httpd_handle_t server, const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t item = {
        .uri = uri,
        .method = method,
        .handler = handler,
        .user_ctx = NULL
    };

    esp_err_t ret = httpd_register_uri_handler(server, &item);
    if (ret != ESP_OK)
    {
        DEBUG_LOGE(TAG, "register failed: %s", uri);
    }
}

void server_app_start(void)
{
    httpd_config_t api_config = HTTPD_DEFAULT_CONFIG();
    api_config.server_port = 80;
    api_config.max_uri_handlers = 16;
    api_config.stack_size = 8192;
    api_config.core_id = APP_CORE_NETWORK;
    api_config.task_priority = APP_TASK_PRIORITY_CONTROL;
    api_config.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&api_server, &api_config);
    if (ret != ESP_OK)
    {
        DEBUG_LOGE(TAG, "API server start failed");
        return;
    }

    register_uri(api_server, "/test", HTTP_GET, test_handler);
    register_uri(api_server, "/disconnect", HTTP_GET, disconnect_handler);
    register_uri(api_server, "/disconnect", HTTP_POST, disconnect_handler);
    register_uri(api_server, "/set_time", HTTP_GET, set_time_handler);
    register_uri(api_server, "/status", HTTP_GET, status_handler);
    register_uri(api_server, "/last_alarm", HTTP_GET, last_alarm_handler);
    register_uri(api_server, "/all_alarms", HTTP_GET, all_alarms_handler);
    register_uri(api_server, "/photo", HTTP_GET, photo_handler);
    register_uri(api_server, "/capture", HTTP_GET, capture_handler);
    register_uri(api_server, "/update_alarm", HTTP_POST, update_alarm_handler);
    register_uri(api_server, "/update_alarm", HTTP_GET, update_alarm_get_handler);
    register_uri(api_server, "/delete_alarm", HTTP_POST, delete_alarm_handler);
    register_uri(api_server, "/reset_alarm", HTTP_GET, reset_alarm_handler);

    httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
    stream_config.server_port = 81;
    stream_config.ctrl_port = 32769;
    stream_config.max_uri_handlers = 4;
    stream_config.stack_size = 8192;
    stream_config.core_id = APP_CORE_NETWORK;
    stream_config.task_priority = APP_TASK_PRIORITY_CAMERA;
    stream_config.lru_purge_enable = true;
    stream_config.send_wait_timeout = 2;

    ret = httpd_start(&stream_server, &stream_config);
    if (ret == ESP_OK)
    {
        register_uri(stream_server, "/stream", HTTP_GET, stream_handler);
        DEBUG_LOGI(TAG, "Stream server started: http://192.168.4.1:81/stream");
    }
    else
    {
        DEBUG_LOGE(TAG, "Stream server start failed");
    }

    xTaskCreatePinnedToCore(
        pc_watchdog_task,
        "pc_watchdog_task",
        4096,
        NULL,
        APP_TASK_PRIORITY_CONTROL,
        NULL,
        APP_CORE_NETWORK
    );

    DEBUG_LOGI(TAG, "API server started: http://192.168.4.1");
}

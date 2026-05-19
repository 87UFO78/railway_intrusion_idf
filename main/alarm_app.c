#include "alarm_app.h"
#include "app_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "sd_app.h"
#include "system_state.h"

static const char *TAG = "alarm_app";
static SemaphoreHandle_t alarm_mutex = NULL;
static char g_lastAlarmJson[256] = {0};

static void make_time_string(char *buffer, int buffer_size)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    snprintf(buffer, buffer_size,
             "%04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900,
             t.tm_mon + 1,
             t.tm_mday,
             t.tm_hour,
             t.tm_min,
             t.tm_sec);
}

static uint32_t json_get_uint(cJSON *obj, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (cJSON_IsNumber(item))
    {
        return (uint32_t)item->valuedouble;
    }
    return 0;
}

void alarm_app_init(void)
{
    alarm_mutex = xSemaphoreCreateMutex();
    if (alarm_mutex == NULL)
    {
        DEBUG_LOGE(TAG, "Alarm mutex create failed");
        return;
    }

    DEBUG_LOGI(TAG, "Alarm app initialized");
}

void alarm_app_load_last_index(void)
{
    char *content = NULL;
    size_t size = 0;

    if (!sd_app_read_alarms(&content, &size))
    {
        DEBUG_LOGW(TAG, "找不到 alarms.json，從 0 開始");
        g_alarmCounter = 0;
        g_imageCounter = 0;
        return;
    }

    char *saveptr = NULL;
    char *line = strtok_r(content, "\n", &saveptr);
    char *last_line = NULL;

    while (line)
    {
        if (strlen(line) > 0)
        {
            last_line = line;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    if (!last_line)
    {
        free(content);
        g_alarmCounter = 0;
        g_imageCounter = 0;
        return;
    }

    cJSON *doc = cJSON_Parse(last_line);
    if (!doc)
    {
        DEBUG_LOGE(TAG, "JSON 解析失敗，從 0 開始");
        free(content);
        return;
    }

    g_alarmCounter = json_get_uint(doc, "AlarmId");
    g_imageCounter = json_get_uint(doc, "ImageId");

    DEBUG_LOGI(TAG, "載入 alarmId=%lu, imageId=%lu",
             (unsigned long)g_alarmCounter,
             (unsigned long)g_imageCounter);

    cJSON_Delete(doc);
    free(content);
}

void alarm_app_add_record(uint32_t alarm_id, uint32_t image_id, const char *result, const char *time_str)
{
    if (alarm_mutex == NULL)
    {
        return;
    }

    xSemaphoreTake(alarm_mutex, portMAX_DELAY);

    char actual_time[32];
    if (!time_str || strlen(time_str) == 0)
    {
        make_time_string(actual_time, sizeof(actual_time));
        time_str = actual_time;
    }

    char json_line[256];
    snprintf(json_line, sizeof(json_line),
             "{\"AlarmId\":%lu,\"ImageId\":%lu,\"Result\":\"%s\",\"Time\":\"%s\"}",
             (unsigned long)alarm_id,
             (unsigned long)image_id,
             result ? result : "未判斷",
             time_str);

    if (sd_app_append_alarm_line(json_line))
    {
        DEBUG_LOGI(TAG, "JSON 已寫入");
    }
    else
    {
        DEBUG_LOGE(TAG, "JSON 寫入失敗");
    }

    snprintf(g_lastAlarmJson, sizeof(g_lastAlarmJson), "%s", json_line);
    g_hasNewAlarm = true;
    system_set_alarm_active(true);

    DEBUG_LOGW(TAG, "建立警報 AlarmId=%lu ImageId=%lu Time=%s",
             (unsigned long)alarm_id,
             (unsigned long)image_id,
             time_str);

    xSemaphoreGive(alarm_mutex);
}

bool alarm_app_get_last_alarm_json(char *buffer, int buffer_size, bool clear_after_read)
{
    if (alarm_mutex == NULL || !buffer || buffer_size <= 0)
    {
        return false;
    }

    xSemaphoreTake(alarm_mutex, portMAX_DELAY);

    if (!g_hasNewAlarm || strlen(g_lastAlarmJson) == 0)
    {
        xSemaphoreGive(alarm_mutex);
        return false;
    }

    snprintf(buffer, buffer_size, "%s", g_lastAlarmJson);

    if (clear_after_read)
    {
        g_hasNewAlarm = false;
        g_lastAlarmJson[0] = '\0';
    }

    xSemaphoreGive(alarm_mutex);
    return true;
}

static char **split_lines(char *content, int *count)
{
    int capacity = 16;
    char **lines = calloc(capacity, sizeof(char *));
    *count = 0;

    char *saveptr = NULL;
    char *line = strtok_r(content, "\n", &saveptr);

    while (line)
    {
        while (*line == '\r' || *line == '\n' || *line == ' ')
        {
            line++;
        }

        if (strlen(line) > 0)
        {
            if (*count >= capacity)
            {
                capacity *= 2;
                lines = realloc(lines, capacity * sizeof(char *));
            }
            lines[*count] = line;
            (*count)++;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    return lines;
}

bool alarm_app_update_from_json(const char *json_body)
{
    if (!json_body)
    {
        return false;
    }

    cJSON *doc = cJSON_Parse(json_body);
    if (!doc)
    {
        return false;
    }

    uint32_t image_id = json_get_uint(doc, "ImageId");
    uint32_t alarm_id = json_get_uint(doc, "AlarmId");
    cJSON *result_item = cJSON_GetObjectItemCaseSensitive(doc, "Result");
    cJSON *time_item = cJSON_GetObjectItemCaseSensitive(doc, "Time");

    const char *result = cJSON_IsString(result_item) ? result_item->valuestring : "未判斷";
    const char *time_str = cJSON_IsString(time_item) ? time_item->valuestring : "";

    if (image_id == 0)
    {
        cJSON_Delete(doc);
        return false;
    }

    char *content = NULL;
    size_t size = 0;
    if (!sd_app_read_alarms(&content, &size))
    {
        cJSON_Delete(doc);
        return false;
    }

    int line_count = 0;
    char **lines = split_lines(content, &line_count);
    bool found = false;

    for (int i = 0; i < line_count; i++)
    {
        cJSON *item = cJSON_Parse(lines[i]);
        if (!item)
        {
            continue;
        }

        uint32_t item_image_id = json_get_uint(item, "ImageId");
        if (item_image_id == image_id)
        {
            found = true;

            cJSON_ReplaceItemInObject(item, "AlarmId", cJSON_CreateNumber(alarm_id));
            cJSON_ReplaceItemInObject(item, "ImageId", cJSON_CreateNumber(image_id));
            cJSON_ReplaceItemInObject(item, "Result", cJSON_CreateString(result));
            if (strlen(time_str) > 0)
            {
                cJSON_ReplaceItemInObject(item, "Time", cJSON_CreateString(time_str));
            }

            char *new_line = cJSON_PrintUnformatted(item);
            lines[i] = new_line;
        }

        cJSON_Delete(item);
    }

    bool ok = false;
    if (found)
    {
        ok = sd_app_rewrite_alarms(lines, line_count);
        DEBUG_LOGI(TAG, "更新警報 AlarmId=%lu Result=%s", (unsigned long)alarm_id, result);

        if (strcmp(result, "不是異物") == 0)
        {
            system_set_alarm_active(false);
        }
    }

    for (int i = 0; i < line_count; i++)
    {
        if (lines[i] < content || lines[i] > content + size)
        {
            free(lines[i]);
        }
    }
    free(lines);
    free(content);
    cJSON_Delete(doc);
    return ok;
}

bool alarm_app_delete_from_json(const char *json_body)
{
    if (!json_body)
    {
        return false;
    }

    cJSON *doc = cJSON_Parse(json_body);
    if (!doc)
    {
        return false;
    }

    uint32_t alarm_id = json_get_uint(doc, "AlarmId");
    uint32_t image_id = json_get_uint(doc, "ImageId");
    cJSON_Delete(doc);

    if (alarm_id == 0 || image_id == 0)
    {
        return false;
    }

    char *content = NULL;
    size_t size = 0;
    if (!sd_app_read_alarms(&content, &size))
    {
        return false;
    }

    int line_count = 0;
    char **lines = split_lines(content, &line_count);
    char **kept = calloc(line_count, sizeof(char *));
    int kept_count = 0;
    bool found = false;

    for (int i = 0; i < line_count; i++)
    {
        cJSON *item = cJSON_Parse(lines[i]);
        if (item)
        {
            uint32_t item_alarm_id = json_get_uint(item, "AlarmId");
            cJSON_Delete(item);

            if (item_alarm_id == alarm_id)
            {
                found = true;
                continue;
            }
        }
        kept[kept_count++] = lines[i];
    }

    bool ok = false;
    if (found)
    {
        ok = sd_app_rewrite_alarms(kept, kept_count);
        sd_app_delete_photo_by_id(image_id);
        alarm_app_load_last_index();
    }

    free(kept);
    free(lines);
    free(content);
    return ok;
}

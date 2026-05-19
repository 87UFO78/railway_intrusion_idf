#include "sd_app.h"
#include "app_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"

#define MOUNT_POINT "/sdcard"
#define SD_MMC_CMD GPIO_NUM_38
#define SD_MMC_CLK GPIO_NUM_39
#define SD_MMC_D0  GPIO_NUM_40

static const char *TAG = "sd_app";
static bool sd_mounted = false;

static void make_photo_path(uint32_t image_id, char *path, size_t path_size)
{
    snprintf(path, path_size, MOUNT_POINT "/photo_%05lu.jpg", (unsigned long)image_id);
}

bool sd_app_init(void)
{
    DEBUG_LOGI(TAG, "Initializing SD card");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card = NULL;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = SD_MMC_CLK;
    slot_config.cmd = SD_MMC_CMD;
    slot_config.d0 = SD_MMC_D0;
    slot_config.width = 1;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(
        MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &card
    );

    if (ret != ESP_OK)
    {
        DEBUG_LOGE(TAG, "SD card mount failed: 0x%x", ret);
        sd_mounted = false;
        return false;
    }

    sdmmc_card_print_info(stdout, card);
    sd_mounted = true;
    DEBUG_LOGI(TAG, "SD card mounted");
    return true;
}

bool sd_app_is_mounted(void)
{
    return sd_mounted;
}

bool sd_app_save_jpg_by_id(uint32_t image_id, camera_fb_t *fb)
{
    if (!sd_mounted || !fb || image_id == 0)
    {
        DEBUG_LOGE(TAG, "SD not ready or invalid fb");
        return false;
    }

    char path[96];
    make_photo_path(image_id, path, sizeof(path));

    FILE *file = fopen(path, "wb");
    if (!file)
    {
        DEBUG_LOGE(TAG, "Failed to open file for writing: %s", path);
        return false;
    }

    size_t written = fwrite(fb->buf, 1, fb->len, file);
    fclose(file);

    if (written != fb->len)
    {
        DEBUG_LOGE(TAG, "File write failed: %s", path);
        return false;
    }

    DEBUG_LOGI(TAG, "Photo saved: %s, size=%d", path, fb->len);
    return true;
}

bool sd_app_read_photo_by_id(uint32_t image_id, uint8_t **buffer, size_t *size)
{
    if (!sd_mounted || image_id == 0 || !buffer || !size)
    {
        return false;
    }

    char path[96];
    make_photo_path(image_id, path, sizeof(path));

    FILE *file = fopen(path, "rb");
    if (!file)
    {
        DEBUG_LOGE(TAG, "Failed to open photo: %s", path);
        return false;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    if (file_size <= 0)
    {
        fclose(file);
        return false;
    }

    uint8_t *data = malloc(file_size);
    if (!data)
    {
        fclose(file);
        DEBUG_LOGE(TAG, "malloc failed for photo");
        return false;
    }

    size_t read_size = fread(data, 1, file_size, file);
    fclose(file);

    if (read_size != file_size)
    {
        free(data);
        return false;
    }

    *buffer = data;
    *size = (size_t)file_size;
    return true;
}

bool sd_app_read_alarms(char **buffer, size_t *size)
{
    if (!sd_mounted || !buffer || !size)
    {
        return false;
    }

    FILE *file = fopen(MOUNT_POINT "/alarms.json", "rb");
    if (!file)
    {
        return false;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    if (file_size <= 0)
    {
        fclose(file);
        return false;
    }

    char *data = malloc(file_size + 1);
    if (!data)
    {
        fclose(file);
        return false;
    }

    size_t read_size = fread(data, 1, file_size, file);
    fclose(file);

    data[read_size] = '\0';
    *buffer = data;
    *size = read_size;
    return true;
}

bool sd_app_append_alarm_line(const char *line)
{
    if (!sd_mounted || !line)
    {
        return false;
    }

    FILE *file = fopen(MOUNT_POINT "/alarms.json", "ab");
    if (!file)
    {
        DEBUG_LOGE(TAG, "Failed to open alarms.json for append");
        return false;
    }

    fwrite(line, 1, strlen(line), file);
    fwrite("\n", 1, 1, file);
    fclose(file);
    return true;
}

bool sd_app_rewrite_alarms(char **lines, int line_count)
{
    if (!sd_mounted)
    {
        return false;
    }

    FILE *file = fopen(MOUNT_POINT "/alarms.json", "wb");
    if (!file)
    {
        DEBUG_LOGE(TAG, "Failed to rewrite alarms.json");
        return false;
    }

    for (int i = 0; i < line_count; i++)
    {
        if (lines[i] && strlen(lines[i]) > 0)
        {
            fwrite(lines[i], 1, strlen(lines[i]), file);
            fwrite("\n", 1, 1, file);
        }
    }

    fclose(file);
    return true;
}

bool sd_app_delete_photo_by_id(uint32_t image_id)
{
    if (!sd_mounted || image_id == 0)
    {
        return false;
    }

    char path[96];
    make_photo_path(image_id, path, sizeof(path));

    int ret = remove(path);
    if (ret == 0)
    {
        DEBUG_LOGI(TAG, "Photo deleted: %s", path);
        return true;
    }

    DEBUG_LOGW(TAG, "Photo delete failed or not found: %s", path);
    return false;
}

#ifndef SD_APP_H
#define SD_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_camera.h"

bool sd_app_init(void);
bool sd_app_is_mounted(void);
bool sd_app_save_jpg_by_id(uint32_t image_id, camera_fb_t *fb);
bool sd_app_read_photo_by_id(uint32_t image_id, uint8_t **buffer, size_t *size);
bool sd_app_read_alarms(char **buffer, size_t *size);
bool sd_app_append_alarm_line(const char *line);
bool sd_app_rewrite_alarms(char **lines, int line_count);
bool sd_app_delete_photo_by_id(uint32_t image_id);

#endif

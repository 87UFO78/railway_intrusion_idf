#ifndef ALARM_APP_H
#define ALARM_APP_H

#include <stdbool.h>
#include <stdint.h>

void alarm_app_init(void);
void alarm_app_load_last_index(void);
void alarm_app_add_record(uint32_t alarm_id, uint32_t image_id, const char *result, const char *time_str);
bool alarm_app_get_last_alarm_json(char *buffer, int buffer_size, bool clear_after_read);
bool alarm_app_update_from_json(const char *json_body);
bool alarm_app_delete_from_json(const char *json_body);

#endif

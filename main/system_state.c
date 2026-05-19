#include "system_state.h"

#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

volatile bool g_pcConnected = false;
volatile bool g_timeSynced = false;
volatile bool g_systemReady = false;
volatile bool g_allowDetect = true;
volatile bool g_alarmActive = false;
volatile bool g_waitIrRelease = false;
volatile uint32_t g_resetCooldownUntilMs = 0;
volatile bool g_hasNewAlarm = false;
volatile bool g_pcOnline = false;
volatile uint32_t g_pcLastSeenMs = 0;

uint32_t g_alarmCounter = 0;
uint32_t g_imageCounter = 0;

void system_state_init(void)
{
    g_pcConnected = false;
    g_timeSynced = false;
    g_systemReady = false;
    g_allowDetect = true;
    g_alarmActive = false;
    g_waitIrRelease = false;
    g_resetCooldownUntilMs = 0;
    g_hasNewAlarm = false;
    g_pcOnline = false;
    g_pcLastSeenMs = 0;
}

uint32_t system_millis(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void system_state_update(void)
{
    g_systemReady = g_pcConnected && g_timeSynced;
}

void system_set_alarm_active(bool active)
{
    g_alarmActive = active;
}

void system_touch_pc_alive(void)
{
    g_pcOnline = true;
    g_pcLastSeenMs = system_millis();
}

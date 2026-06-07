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

static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t connection_generation = 1;

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
    connection_generation = 1;
}

uint32_t system_millis(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void system_state_update(void)
{
    portENTER_CRITICAL(&state_lock);
    g_systemReady = g_pcConnected && g_timeSynced && g_pcOnline;
    portEXIT_CRITICAL(&state_lock);
}

void system_set_alarm_active(bool active)
{
    g_alarmActive = active;
}

void system_touch_pc_alive(void)
{
    uint32_t now = system_millis();

    portENTER_CRITICAL(&state_lock);
    if (g_pcConnected)
    {
        g_pcOnline = true;
        g_pcLastSeenMs = now;
    }
    portEXIT_CRITICAL(&state_lock);
}

void system_mark_pc_offline(void)
{
    portENTER_CRITICAL(&state_lock);
    connection_generation++;
    g_pcOnline = false;
    g_pcConnected = false;
    g_timeSynced = false;
    g_systemReady = false;
    g_allowDetect = false;
    g_alarmActive = false;
    g_waitIrRelease = true;
    portEXIT_CRITICAL(&state_lock);
}

uint32_t system_connection_generation(void)
{
    portENTER_CRITICAL(&state_lock);
    uint32_t generation = connection_generation;
    portEXIT_CRITICAL(&state_lock);
    return generation;
}

bool system_connection_is_active(uint32_t generation)
{
    portENTER_CRITICAL(&state_lock);
    bool active =
        generation == connection_generation &&
        g_pcConnected &&
        g_timeSynced &&
        g_systemReady &&
        g_pcOnline;
    portEXIT_CRITICAL(&state_lock);
    return active;
}

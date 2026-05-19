#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <stdbool.h>
#include <stdint.h>

extern volatile bool g_pcConnected;
extern volatile bool g_timeSynced;
extern volatile bool g_systemReady;
extern volatile bool g_allowDetect;
extern volatile bool g_alarmActive;
extern volatile bool g_waitIrRelease;
extern volatile uint32_t g_resetCooldownUntilMs;
extern volatile bool g_hasNewAlarm;
extern volatile bool g_pcOnline;
extern volatile uint32_t g_pcLastSeenMs;

extern uint32_t g_alarmCounter;
extern uint32_t g_imageCounter;

void system_state_init(void);
void system_state_update(void);
void system_set_alarm_active(bool active);
void system_touch_pc_alive(void);
uint32_t system_millis(void);

#endif

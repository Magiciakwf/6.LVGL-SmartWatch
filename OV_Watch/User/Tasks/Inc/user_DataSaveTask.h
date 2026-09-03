#ifndef __USER_DATASAVETASK_H__
#define __USER_DATASAVETASK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "user_TasksInit.h"

#define DATA_SAVE_EVENT_SETTINGS_CHANGED 1U
#define DATA_SAVE_EVENT_TIME_CHANGED     2U
#define DATA_SAVE_EVENT_LEGACY_PERIODIC  3U
#define DATA_SAVE_EVENT_FORCE            4U

void DataSaveTask(void *argument);
void DataSave_Request(uint8_t event);

	
#ifdef __cplusplus
}
#endif

#endif

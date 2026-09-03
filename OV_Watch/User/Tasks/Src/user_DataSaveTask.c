/* Private includes -----------------------------------------------------------*/
#include "user_DataSaveTask.h"
#include "ui_DateTimeSetPage.h"
#include "main.h"
#include "rtc.h"
#include "DataSave.h"
#include "HWDataAccess.h"

#define STEP_SAVE_DELTA          100U
#define STEP_SAVE_MAX_INTERVAL  300000UL
#define DATA_SAVE_POLL_TICKS     1000U

static void rtc_date_get(RTC_DateTypeDef *date)
{
	RTC_TimeTypeDef time;

	/* STM32 RTC requires a time read before the date shadow register read. */
	HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);
	HAL_RTC_GetDate(&hrtc, date, RTC_FORMAT_BIN);
}

static uint8_t same_date(const EEPROM_UserData_t *data, const RTC_DateTypeDef *date)
{
	return (uint8_t)(data->year == date->Year &&
					 data->month == date->Month &&
					 data->day == date->Date);
}

void DataSave_Request(uint8_t event)
{
	if(DataSave_MessageQueue != NULL)
		(void)osMessageQueuePut(DataSave_MessageQueue, &event, 0U, 0U);
}

/**
  * @brief  Persist settings and daily steps using CRC-protected A/B records.
  * @param  argument: Not used
  * @retval None
  */
void DataSaveTask(void *argument)
{
	EEPROM_UserData_t saved;
	RTC_DateTypeDef nowdate;
	uint32_t last_save_tick;
	uint8_t event;

	(void)argument;
	rtc_date_get(&nowdate);
	if(EEPROM_UserDataLoad(&saved, nowdate.Year, nowdate.Month, nowdate.Date))
	{
		saved.wrist_enabled = HWInterface.IMU.wrist_is_enabled ? 1U : 0U;
		saved.app_sync_enabled = ui_APPSy_EN ? 1U : 0U;
		saved.year = nowdate.Year;
		saved.month = nowdate.Month;
		saved.day = nowdate.Date;
		saved.steps = HWInterface.IMU.Steps;
	}
	/* The RTOS tick is stepped across tickless idle; the HAL tick is suspended. */
	last_save_tick = osKernelGetTickCount();

	while(1)
	{
		EEPROM_UserData_t candidate = saved;
		uint16_t current_steps;
		uint32_t now_tick;
		uint8_t force_save = 0U;
		uint8_t settings_changed;

		event = 0U;
		(void)osMessageQueueGet(DataSave_MessageQueue, &event, NULL, DATA_SAVE_POLL_TICKS);
		rtc_date_get(&nowdate);

		settings_changed = (uint8_t)(saved.wrist_enabled != (HWInterface.IMU.wrist_is_enabled ? 1U : 0U) ||
									 saved.app_sync_enabled != (ui_APPSy_EN ? 1U : 0U));
		candidate.wrist_enabled = HWInterface.IMU.wrist_is_enabled ? 1U : 0U;
		candidate.app_sync_enabled = ui_APPSy_EN ? 1U : 0U;

		if(!same_date(&saved, &nowdate))
		{
			/* Natural midnight and APP/manual date jumps use the same rule. */
			if(!HWInterface.IMU.ConnectionError)
				HWInterface.IMU.SetSteps(0UL);
			HWInterface.IMU.Steps = 0U;
			candidate.year = nowdate.Year;
			candidate.month = nowdate.Month;
			candidate.day = nowdate.Date;
			candidate.steps = 0U;
			force_save = 1U;
		}
		else
		{
			current_steps = HWInterface.IMU.Steps;
			if(!HWInterface.IMU.ConnectionError)
				current_steps = HWInterface.IMU.GetSteps();

			/* A same-day IMU reset must not overwrite a newer persisted count. */
			if(current_steps < saved.steps)
			{
				current_steps = saved.steps;
				if(!HWInterface.IMU.ConnectionError)
					HWInterface.IMU.SetSteps((unsigned long)current_steps);
			}
			HWInterface.IMU.Steps = current_steps;
			candidate.steps = current_steps;
		}

		now_tick = osKernelGetTickCount();
		if(event == DATA_SAVE_EVENT_FORCE)
			force_save = 1U;
		if(candidate.steps >= saved.steps &&
		   (uint16_t)(candidate.steps - saved.steps) >= STEP_SAVE_DELTA)
			force_save = 1U;
		if(candidate.steps != saved.steps &&
		   (uint32_t)(now_tick - last_save_tick) >= STEP_SAVE_MAX_INTERVAL)
			force_save = 1U;

		/* Also catch a lost/full settings event queue on the next periodic poll. */
		if(settings_changed)
			force_save = 1U;

		if(force_save && !EEPROM_UserDataSave(&candidate))
		{
			saved = candidate;
			last_save_tick = now_tick;
		}
	}
}

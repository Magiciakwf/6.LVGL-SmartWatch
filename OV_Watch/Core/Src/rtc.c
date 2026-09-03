/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rtc.c
  * @brief   This file provides code for the configuration
  *          of the RTC instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "rtc.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

RTC_HandleTypeDef hrtc;

/* RTC init function */
void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */
	/* A wake timer may still be enabled in the backup domain by older firmware. */
	(void)HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
	__HAL_RTC_WAKEUPTIMER_EXTI_CLEAR_FLAG();

	if(HAL_RTCEx_BKUPRead(&hrtc,RTC_BKP_DR0)!=0X5050)//�Ƿ��һ������
  {
  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x11;
  sTime.Minutes = 0x59;
  sTime.Seconds = 0x55;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_SUNDAY;
  sDate.Month = RTC_MONTH_MAY;
  sDate.Date = 0x21;
  sDate.Year = 0x23;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }

  /* The calendar runs in STOP, but periodic wake-up is intentionally disabled.
   * Key, charge-state and IMU EXTI lines are the application wake sources. */
  /* USER CODE BEGIN RTC_Init 2 */
	HAL_RTCEx_BKUPWrite(&hrtc,RTC_BKP_DR0,0X5050);//����Ѿ���ʼ������
  }
  /* USER CODE END RTC_Init 2 */

}

void HAL_RTC_MspInit(RTC_HandleTypeDef* rtcHandle)
{

  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspInit 0 */

  /* USER CODE END RTC_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* RTC clock enable */
    __HAL_RCC_RTC_ENABLE();

    /* RTC wake-up IRQ is left disabled until an alarm/timer is explicitly used. */
    HAL_NVIC_DisableIRQ(RTC_WKUP_IRQn);
  /* USER CODE BEGIN RTC_MspInit 1 */

  /* USER CODE END RTC_MspInit 1 */
  }
}

void HAL_RTC_MspDeInit(RTC_HandleTypeDef* rtcHandle)
{

  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspDeInit 0 */

  /* USER CODE END RTC_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_RTC_DISABLE();

    /* RTC interrupt Deinit */
    HAL_NVIC_DisableIRQ(RTC_WKUP_IRQn);
  /* USER CODE BEGIN RTC_MspDeInit 1 */

  /* USER CODE END RTC_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
uint8_t weekday_calculate(int y,int m,int d,int c)
{
	int w;
	if (m == 1 || m == 2)
	{y--, m += 12;}
	w = y + y / 4 + c / 4  + 26*(m + 1)/10 + d - 1 - 2 * c;
	while(w<0)
		w+=7;
	w%=7;
	w=(w==0)?7:w;
	return w;
}

void RTC_SetTime(uint8_t hours, uint8_t minutes, uint8_t seconds)
{
	RTC_TimeTypeDef time={0};//������Ϊ{0}ʱ��ͻ�����
	time.Hours=hours;
	time.Minutes=minutes;
	time.Seconds=seconds;
	if (HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
}

void RTC_SetDate(uint8_t year, uint8_t month, uint8_t date)
{
	RTC_DateTypeDef setdate={0};//������Ϊ{0}ʱ��ͻ�����
	setdate.Date=date;
	setdate.Month=month;
	setdate.Year=year;

	//culculat the weekday
	setdate.WeekDay = weekday_calculate(year,month,date,20);

	if (HAL_RTC_SetDate(&hrtc, &setdate, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
}
/* USER CODE END 1 */

/* Private includes -----------------------------------------------------------*/
//includes
#include "user_TasksInit.h"
//sys
#include "sys.h"
#include "stdio.h"
#include "lcd.h"
#include "WDOG.h"
//gui
#include "lvgl.h"
/* Optional stopwatch UI is outside the core architecture. */
//tasks
#include "user_HardwareInitTask.h"
#include "user_RunModeTasks.h"
#include "user_KeyTask.h"
#include "user_ScrRenewTask.h"
#include "user_SensUpdateTask.h"
#include "user_ChargCheckTask.h"
#include "user_MessageSendTask.h"
#include "user_DataSaveTask.h"
#include "ota_app.h"

/* Private typedef -----------------------------------------------------------*/


/* Private define ------------------------------------------------------------*/
#define UART_RX_MESSAGE_BUFFER_SIZE 128U

/* Private variables ---------------------------------------------------------*/
static StaticMessageBuffer_t UartRxMessageBufferControl;
static uint8_t UartRxMessageBufferStorage[UART_RX_MESSAGE_BUFFER_SIZE];
MessageBufferHandle_t UartRxMessageBuffer;

/* Timers --------------------------------------------------------------------*/
osTimerId_t IdleTimerHandle;


/* Tasks ---------------------------------------------------------------------*/
// Hardwares initialization
osThreadId_t HardwareInitTaskHandle;
const osThreadAttr_t HardwareInitTask_attributes = {
  .name = "HardwareInitTask",
  .stack_size = 128 * 10,
  .priority = (osPriority_t) osPriorityHigh3,
};

//LVGL Handler task
osThreadId_t LvHandlerTaskHandle;
const osThreadAttr_t LvHandlerTask_attributes = {
  .name = "LvHandlerTask",
  .stack_size = 128 * 24,
  .priority = (osPriority_t) osPriorityLow,
};

//WDOG Feed task
osThreadId_t WDOGFeedTaskHandle;
const osThreadAttr_t WDOGFeedTask_attributes = {
  .name = "WDOGFeedTask",
  .stack_size = 128 * 1,
  .priority = (osPriority_t) osPriorityHigh2,
};

//Idle Enter Task
osThreadId_t IdleEnterTaskHandle;
const osThreadAttr_t IdleEnterTask_attributes = {
  .name = "IdleEnterTask",
  .stack_size = 128 * 1,
  .priority = (osPriority_t) osPriorityHigh,
};

//Stop Enter Task
osThreadId_t StopEnterTaskHandle;
const osThreadAttr_t StopEnterTask_attributes = {
  .name = "StopEnterTask",
  .stack_size = 128 * 16,
  .priority = (osPriority_t) osPriorityHigh1,
};

//Key task
osThreadId_t KeyTaskHandle;
const osThreadAttr_t KeyTask_attributes = {
  .name = "KeyTask",
  .stack_size = 128 * 1,
  .priority = (osPriority_t) osPriorityNormal,
};

//ScrRenew task
osThreadId_t ScrRenewTaskHandle;
const osThreadAttr_t ScrRenewTask_attributes = {
  .name = "ScrRenewTask",
  .stack_size = 128 * 10,
  .priority = (osPriority_t) osPriorityLow1,
};

//SensorDataRenew task
osThreadId_t SensorDataTaskHandle;
const osThreadAttr_t SensorDataTask_attributes = {
  .name = "SensorDataTask",
  .stack_size = 128 * 5,
  .priority = (osPriority_t) osPriorityLow1,
};

/* Optional heart-rate task; retained as disabled reference code. */
#if 0
osThreadId_t HRDataTaskHandle;
const osThreadAttr_t HRDataTask_attributes = {
  .name = "HRDataTask",
  .stack_size = 128 * 5,
  .priority = (osPriority_t) osPriorityLow1,
};
#endif /* optional HR task */

//ChargPageEnterTask
osThreadId_t ChargPageEnterTaskHandle;
const osThreadAttr_t ChargPageEnterTask_attributes = {
  .name = "ChargPageEnterTask",
  .stack_size = 128 * 10,
  .priority = (osPriority_t) osPriorityLow1,
};

//messagesendtask
osThreadId_t MessageSendTaskHandle;
const osThreadAttr_t MessageSendTask_attributes = {
  .name = "MessageSendTask",
  .stack_size = 128 * 5,
  .priority = (osPriority_t) osPriorityLow1,
};

//MPUCheckTask
osThreadId_t MPUCheckTaskHandle;
const osThreadAttr_t MPUCheckTask_attributes = {
  .name = "MPUCheckTask",
  .stack_size = 128 * 3,
  .priority = (osPriority_t) osPriorityLow2,
};

//DataSaveTask
osThreadId_t DataSaveTaskHandle;
const osThreadAttr_t DataSaveTask_attributes = {
  .name = "DataSaveTask",
  .stack_size = 128 * 5,
  .priority = (osPriority_t) osPriorityLow2,
};

// OneNET OTA task
osThreadId_t OtaTaskHandle;
const osThreadAttr_t OtaTask_attributes = {
  .name = "OtaTask",
  .stack_size = 128 * 16,
  .priority = (osPriority_t) osPriorityLow2,
};


/* Message queues ------------------------------------------------------------*/
//Key message
osMessageQueueId_t Key_MessageQueue;
osMessageQueueId_t Idle_MessageQueue;
osMessageQueueId_t Stop_MessageQueue;
osMessageQueueId_t IdleBreak_MessageQueue;
osMessageQueueId_t HomeUpdata_MessageQueue;
osMessageQueueId_t DataSave_MessageQueue;

/* Private function prototypes -----------------------------------------------*/
void LvHandlerTask(void *argument);
void WDOGFeedTask(void *argument);

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
 void User_Tasks_Init(void)
{
  /* add mutexes, ... */

  /* add semaphores, ... */

  /* start timers, add new ones, ... */
	//osTimerPeriodic周期性触发，osTimerOnce只触发一次
	IdleTimerHandle = osTimerNew(IdleTimerCallback, osTimerPeriodic, NULL, NULL);
	osTimerStart(IdleTimerHandle,100);//100ms

  /* add queues, ... */
	/* A message buffer is a frame-preserving specialization of a FreeRTOS
	 * stream buffer.  Each UART IDLE event is delivered as one complete frame. */
	UartRxMessageBuffer = xMessageBufferCreateStatic(
		UART_RX_MESSAGE_BUFFER_SIZE,
		UartRxMessageBufferStorage,
		&UartRxMessageBufferControl);
	configASSERT(UartRxMessageBuffer != NULL);

	Key_MessageQueue  = osMessageQueueNew(1, 1, NULL);
	Idle_MessageQueue = osMessageQueueNew(1, 1, NULL);
	Stop_MessageQueue = osMessageQueueNew(1, 1, NULL);
	IdleBreak_MessageQueue = osMessageQueueNew(1, 1, NULL);
	HomeUpdata_MessageQueue = osMessageQueueNew(1, 1, NULL);
	DataSave_MessageQueue = osMessageQueueNew(4, 1, NULL);

	/* add threads, ... */
  HardwareInitTaskHandle  = osThreadNew(HardwareInitTask, NULL, &HardwareInitTask_attributes);//43
  LvHandlerTaskHandle  = osThreadNew(LvHandlerTask, NULL, &LvHandlerTask_attributes);//8
  // WDOGFeedTaskHandle   = osThreadNew(WDOGFeedTask, NULL, &WDOGFeedTask_attributes);
	IdleEnterTaskHandle  = osThreadNew(IdleEnterTask, NULL, &IdleEnterTask_attributes);//40
	StopEnterTaskHandle  = osThreadNew(StopEnterTask, NULL, &StopEnterTask_attributes);//41
	KeyTaskHandle 			 = osThreadNew(KeyTask, NULL, &KeyTask_attributes);//24
	ScrRenewTaskHandle   = osThreadNew(ScrRenewTask, NULL, &ScrRenewTask_attributes);//9
	SensorDataTaskHandle = osThreadNew(SensorDataUpdateTask, NULL, &SensorDataTask_attributes);//9
	/* HRDataTaskHandle = osThreadNew(HRDataUpdateTask, NULL, &HRDataTask_attributes); */
	ChargPageEnterTaskHandle = osThreadNew(ChargPageEnterTask, NULL, &ChargPageEnterTask_attributes);//9
  MessageSendTaskHandle = osThreadNew(MessageSendTask, NULL, &MessageSendTask_attributes);//9
	MPUCheckTaskHandle		= osThreadNew(MPUCheckTask, NULL, &MPUCheckTask_attributes);//10
	DataSaveTaskHandle		= osThreadNew(DataSaveTask, NULL, &DataSaveTask_attributes);//10
	OtaTaskHandle          = osThreadNew(ota_app_task, NULL, &OtaTask_attributes);//10

  /* add events, ... */


	/* add  others ... */
	uint8_t HomeUpdataStr;
	osMessageQueuePut(HomeUpdata_MessageQueue, &HomeUpdataStr, 0, 1);

}


/**
  * @brief  FreeRTOS Tick Hook, to increase the LVGL tick
  * @param  None
  * @retval None
  */
void TaskTickHook(void)
{
	/* LVGL uses osKernelGetTickCount(), which remains correct in tickless idle. */
}


/**
  * @brief  LVGL Handler task, to run the lvgl
  * @param  argument: Not used
  * @retval None
  */
void LvHandlerTask(void *argument)
{
	uint8_t IdleBreakstr=0;
  while(1)
  {
		uint32_t HandlerDelay;

		if(lv_disp_get_inactive_time(NULL)<1000)
		{
			//Idle time break, set to 0
			osMessageQueuePut(IdleBreak_MessageQueue, &IdleBreakstr, 0, 0);
		}
		HandlerDelay = lv_task_handler();
		if(HandlerDelay < 5U)
		{
			HandlerDelay = 5U;
		}
		else if(HandlerDelay > 20U)
		{
			HandlerDelay = 20U;
		}
		osDelay(HandlerDelay);
	}
}


/**
  * @brief  Watch Dog Feed task
  * @param  argument: Not used
  * @retval None
  */
void WDOGFeedTask(void *argument)
{
	//owdg
	// WDOG_Port_Init();
  while(1)
  {
		// WDOG_Feed();
		// WDOG_Enable();
    osDelay(100);
  }
}

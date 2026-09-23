#ifndef __USER_TASKSINIT_H__
#define __USER_TASKSINIT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "cmsis_os.h"
#include "message_buffer.h"

#define SCRRENEW_DEPTH	5

extern osMessageQueueId_t Key_MessageQueue;
extern osMessageQueueId_t Idle_MessageQueue;
extern osMessageQueueId_t Stop_MessageQueue;
extern osMessageQueueId_t IdleBreak_MessageQueue;
extern osMessageQueueId_t HomeUpdata_MessageQueue;
extern osMessageQueueId_t DataSave_MessageQueue;

/* Tasks that are woken directly by EXTI interrupt handlers. */
extern osThreadId_t KeyTaskHandle;
extern osThreadId_t ChargPageEnterTaskHandle;
extern osThreadId_t MPUCheckTaskHandle;

/* UART IDLE frames are copied here by the ISR and parsed by MessageSendTask. */
extern MessageBufferHandle_t UartRxMessageBuffer;

void User_Tasks_Init(void);
void TaskTickHook(void);

#ifdef __cplusplus
}
#endif

#endif

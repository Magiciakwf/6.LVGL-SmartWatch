/* Private includes -----------------------------------------------------------*/
//includes
#include "user_TasksInit.h"
#include "ui_HomePage.h"
#include "main.h"
#include "stm32f4xx_it.h"
#include "key.h"
#include "power.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/


/**
  * @brief  Key press check task
  * @param  argument: Not used
  * @retval None
  */
void KeyTask(void *argument)
{
	uint8_t keystr=0;
	uint8_t Stopstr=0;
	uint8_t IdleBreakstr=0;
	uint8_t long_press_tracking=0;
	uint8_t shutdown_requested=0;
	uint32_t key_press_started=0U;
	while(1)
	{
		/* Keep application work out of the 1 kHz HAL interrupt. */
		if(!KEY1)
		{
			if(long_press_tracking == 0U)
			{
				long_press_tracking = 1U;
				shutdown_requested = 0U;
				key_press_started = osKernelGetTickCount();
			}
			else if((shutdown_requested == 0U) &&
					((osKernelGetTickCount() - key_press_started) >= 3000U))
			{
				shutdown_requested = 1U;
				Power_DisEnable();
			}
		}
		else
		{
			long_press_tracking = 0U;
			shutdown_requested = 0U;
		}

		switch(KeyScan(0))
		{
			case 1:
				keystr = 1;
				osMessageQueuePut(Key_MessageQueue, &keystr, 0, 1);
				osMessageQueuePut(IdleBreak_MessageQueue, &IdleBreakstr, 0, 1);
				break;

			case 2:
				if(Page_Get_NowPage()->page_obj == &ui_HomePage)
				{
					osMessageQueuePut(Stop_MessageQueue, &Stopstr, 0, 1);
				}
				else
				{
					keystr = 2;
					osMessageQueuePut(Key_MessageQueue, &keystr, 0, 1);
					osMessageQueuePut(IdleBreak_MessageQueue, &IdleBreakstr, 0, 1);
				}
				break;
		}
		HardInt_key_flag = 0U;
		osDelay(10);
	}
}

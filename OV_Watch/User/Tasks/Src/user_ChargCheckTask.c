/* Private includes -----------------------------------------------------------*/
//includes
#include "user_TasksInit.h"
#include "user_ScrRenewTask.h"
#include "user_RunModeTasks.h"
#include "ui_HomePage.h"
#include "ui_ChargPage.h"
#include "main.h"
#include "HWDataAccess.h"
#include "stm32f4xx_it.h"
/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/


/**
  * @brief  charg page enter task
  * @param  argument: Not used
  * @retval None
  */
void ChargPageEnterTask(void *argument)
{
	while(1)
	{
		/* EXTI2 gives the notification; no 500 ms flag polling is required. */
		(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

		taskENTER_CRITICAL();
		IdleTimerCount = 0;
		taskEXIT_CRITICAL();

		HardInt_Charg_flag = 0U;
		if((ChargeCheck()) && (Page_Get_NowPage()->page_obj != &ui_ChargPage))
		{
			Page_Load(&Page_Charg);
		}
		else if((!ChargeCheck()) && (Page_Get_NowPage()->page_obj == &ui_ChargPage))
		{
			Page_Back();
		}
	}
}



//strcpy

//返回值写错了，需要为字符串开头地址
//最后一个没有补'\0'
char *strcpy(char *dest,char *src)
{
	if(src == NULL)
	{
		return NULL;
	}

	char *ret = dest;

	while(*src!='\0')
	{
		*dest++ = *src++;
	}
	*dest = '\0';
	return ret;
}
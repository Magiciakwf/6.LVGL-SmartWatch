/* Private includes -----------------------------------------------------------*/
//includes
#include "user_TasksInit.h"

#include "ui.h"
#include "ui_HomePage.h"
#include "ui_OffTimePage.h"

#include "main.h"
#include "stm32f4xx_it.h"
#include "usart.h"
#include "tim.h"
#include "lcd_init.h"
#include "power.h"
#include "CST816.h"
#include "MPU6050.h"
#include "key.h"

#include "HWDataAccess.h"
/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
uint16_t IdleTimerCount = 0;

/* Private function prototypes -----------------------------------------------*/
extern TIM_HandleTypeDef htim1;
extern void SystemClock_Config(void);
static void LowPower_ArmWakeSources(void);
static void LowPower_EnterStopMode(void);
static uint8_t LowPower_IsUserWakeEvent(void);

static void LowPower_ArmWakeSources(void)
{
	/* Remove stale EXTI requests before arming the next STOP interval. */
	__HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_12);
	HAL_NVIC_ClearPendingIRQ(EXTI0_IRQn);
	HAL_NVIC_ClearPendingIRQ(EXTI2_IRQn);
	HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
}

static uint8_t LowPower_IsUserWakeEvent(void)
{
	return (uint8_t)((HardInt_key_flag != 0U) ||
						 (!KEY1) ||
						 (HardInt_Charg_flag != 0U));
}

static void LowPower_EnterStopMode(void)
{
	/* TIM1 is the HAL time base; SysTick is the FreeRTOS time base. */
	//暂停两套系统时钟
	HAL_SuspendTick();
	CLEAR_BIT(SysTick->CTRL, SysTick_CTRL_TICKINT_Msk);
	//清除遗留的Systick挂起异常
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
	//清除定时器1的更新标志
	__HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);

	/* Flash power-down plus the low-power regulator minimizes STOP current. */
	HAL_DBGMCU_DisableDBGStopMode();
	HAL_PWREx_EnableFlashPowerDown();
	__HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);

	/* Closing this race prevents an event just before WFI from being lost. */
	__disable_irq();
	if(!LowPower_IsUserWakeEvent() && (HardInt_mpu_flag == 0U))
	{
		//进入STOP模式
		HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
	}
	__enable_irq();
	__ISB();

	//恢复PLL，HAL_TICK,SysTick
	/* HAL clock recovery uses HAL_GetTick() for oscillator timeouts. */
	HAL_ResumeTick();
	/* STOP switches SYSCLK back to HSI. Restore the 100 MHz run clock. */

	SystemClock_Config();
	SET_BIT(SysTick->CTRL, SysTick_CTRL_TICKINT_Msk);
}

/* Tasks ---------------------------------------------------------------------*/

/**
	* @brief  Enter Idle state
  * @param  argument: Not used
  * @retval None
  */
void IdleEnterTask(void *argument)
{
	uint8_t Idlestr=0;
	uint8_t IdleBreakstr=0;
	while(1)
	{
		//light get dark
		if(osMessageQueueGet(Idle_MessageQueue,&Idlestr,NULL,1)==osOK)
		{
			LCD_Set_Light(5);
		}
		//resume light if light got dark and idle state breaked by key pressing or screen touching
		if(osMessageQueueGet(IdleBreak_MessageQueue,&IdleBreakstr,NULL,1)==osOK)
		{
			taskENTER_CRITICAL();
			IdleTimerCount = 0;
			taskEXIT_CRITICAL();
			LCD_Set_Light(ui_LightSliderValue);
		}
		osDelay(10);
	}
}

/**
  * @brief  enter the stop mode and resume
  * @param  argument: Not used
  * @retval None
  */
void StopEnterTask(void *argument)
{
	uint8_t Stopstr = 0U;
	while(1)
	{
		/* This task has no periodic work; block until a STOP request arrives. */
		if(osMessageQueueGet(Stop_MessageQueue, &Stopstr, NULL, osWaitForever) == osOK)
		{
			uint8_t WakeRequested = 0U;
			uint8_t ImuWasPutToSleep = 0U;

			taskENTER_CRITICAL();
			IdleTimerCount = 0;
			taskEXIT_CRITICAL();
			LowPower_ArmWakeSources();

			/* Stop DMA/USART through the public HAL API so handle state remains valid. */
			HardInt_receive_str[0] = 0U;
			(void)HAL_UART_DeInit(&huart1);

			/* Backlight first, then place the ST7789 controller in Sleep In mode. */
			LCD_Close_Light();
			LCD_ST7789_SleepIn();

			/* Deep touch sleep intentionally leaves key/charge/IMU as wake sources. */
			CST816_Sleep();

			/* Keep the IMU's cyclic low-power mode only when wrist wake is requested. */
			if((HWInterface.IMU.ConnectionError == 0U) &&
			   (HWInterface.IMU.wrist_is_enabled == 0U))
			{
				MPU_Sleep();
				ImuWasPutToSleep = 1U;
			}

			/* No task may run while the MCU clocks and peripherals are suspended. */
			vTaskSuspendAll();

			do
			{
				uint8_t WristWake = 0U;

				LowPower_EnterStopMode();

				if((HWInterface.IMU.ConnectionError == 0U) &&
				   (HWInterface.IMU.wrist_is_enabled != 0U) &&
				   (HardInt_mpu_flag != 0U))
				{
					uint8_t Horizontal = MPU_isHorizontal();
					if((Horizontal != 0U) &&
					   (HWInterface.IMU.wrist_state == WRIST_DOWN))
					{
						HWInterface.IMU.wrist_state = WRIST_UP;
						WristWake = 1U;
					}
					else if(Horizontal == 0U)
					{
						HWInterface.IMU.wrist_state = WRIST_DOWN;
					}
				}
				//LowPower_IsUserWakeEvent为用户唤醒事件
				WakeRequested = (uint8_t)(LowPower_IsUserWakeEvent() || WristWake);
				HardInt_key_flag = 0U;
				HardInt_mpu_flag = 0U;
			} while(WakeRequested == 0U);

			(void)xTaskResumeAll();

			/* Restore each peripheral through the same initialization path as boot. */
			MX_USART1_UART_Init();
			(void)HAL_UARTEx_ReceiveToIdle_DMA(&huart1,
									   (uint8_t *)HardInt_receive_str,
									   sizeof(HardInt_receive_str));
			__HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);

			if(ImuWasPutToSleep != 0U)
			{
				MPU_Wakeup();
			}

			LCD_ST7789_SleepOut();
			LCD_Open_Light();
			LCD_Set_Light(ui_LightSliderValue);
			CST816_Wakeup();

			/* Preserve the charge event for ChargPageEnterTask to consume. */
#if HW_USE_BAT
			if(ChargeCheck())
			{
				HardInt_Charg_flag = 1U;
				xTaskNotifyGive((TaskHandle_t)ChargPageEnterTaskHandle);
			}
#endif

			{
				uint8_t HomeUpdataStr = 1U;
				(void)osMessageQueuePut(HomeUpdata_MessageQueue,
									&HomeUpdataStr, 0U, 1U);
			}
		}
	}
}

void IdleTimerCallback(void *argument)
{
	//临界区保护防止并发修改
	taskENTER_CRITICAL();
	IdleTimerCount+=1;
	taskEXIT_CRITICAL();
	//make sure the LightOffTime<TurnOffTime
	if(IdleTimerCount == (ui_LTimeValue*10))
	{
		uint8_t Idlestr=0;
		//send the Light off message
		osMessageQueuePut(Idle_MessageQueue, &Idlestr, 0, 1);

	}
	if(IdleTimerCount == (ui_TTimeValue*10))
	{
		uint8_t Stopstr = 1;
		IdleTimerCount  = 0;
		//send the Stop message
		osMessageQueuePut(Stop_MessageQueue, &Stopstr, 0, 1);
	}
}

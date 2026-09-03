/* Private includes -----------------------------------------------------------*/

// includes
// sys
#include "usart.h"
#include "tim.h"
#include "stm32f4xx_it.h"
#include "delay.h"

// user
#include "user_TasksInit.h"
#include "HWDataAccess.h"
#include "version.h"

// bsp
#include "key.h"
#include "lcd.h"
#include "lcd_init.h"
#include "CST816.h"
#include "DataSave.h"
#include "ota_app.h"

// ui
//gui
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "ui.h"

// APP SYS setting
#include "ui_DateTimeSetPage.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/


/**
  * @brief  hardwares init task
  * @param  argument: Not used
  * @retval None
  */
void HardwareInitTask(void *argument)
{
	while(1)
	{
    vTaskSuspendAll();//挂起任务调度，防止初始化被打断

    /* RTC calendar remains active in STOP. Wake-up is event driven by
     * key, charge-state or IMU EXTI instead of a wasteful 1-second timer. */

    // usart start
    HAL_UART_Receive_DMA(&huart1,(uint8_t*)HardInt_receive_str,25);
    __HAL_UART_ENABLE_IT(&huart1,UART_IT_IDLE);

    // PWM Start
    HAL_TIM_PWM_Start(&htim3,TIM_CHANNEL_3);//屏幕无法显示的问题根源

    // sys delay
    delay_init();
    // wait
    delay_ms(1000);

    // power
    HWInterface.Power.Init();

    // key
    Key_Port_Init();

    // sensors
    uint8_t num = 3;

    /* Optional environmental, compass and heart-rate sensors are retained as
     * disabled reference code; MPU6050 remains active below. */
#if 0
    num = 3;
    while(num && HWInterface.AHT21.ConnectionError)
    {
      num--;
      HWInterface.AHT21.ConnectionError = HWInterface.AHT21.Init();
    }

    num = 3;
    while(num && HWInterface.Ecompass.ConnectionError)
    {
      num--;
      HWInterface.Ecompass.ConnectionError = HWInterface.Ecompass.Init();
    }
    if(!HWInterface.Ecompass.ConnectionError)
      HWInterface.Ecompass.Sleep();

    num = 3;
    while(num && HWInterface.Barometer.ConnectionError)
    {
      num--;
      HWInterface.Barometer.ConnectionError = HWInterface.Barometer.Init();
    }

#endif /* optional environmental/compass sensors */

    num = 3;
    while(num && HWInterface.IMU.ConnectionError)
    {
      num--;
      HWInterface.IMU.ConnectionError = HWInterface.IMU.Init();
      // Sensor_MPU_Erro = MPU_Init();
    }

#if 0 /* optional heart-rate sensor */
    num = 3;
    while(num && HWInterface.HR_meter.ConnectionError)
    {
      num--;
      HWInterface.HR_meter.ConnectionError = HWInterface.HR_meter.Init();
    }
    if(!HWInterface.HR_meter.ConnectionError)
      HWInterface.HR_meter.Sleep();
#endif /* optional sensors */


    // EEPROM user data: load the newest valid A/B record and restore settings.
    EEPROM_Init();
    {
      RTC_TimeTypeDef nowtime;
      RTC_DateTypeDef nowdate;
      EEPROM_UserData_t user_data;

      /* Read time before date to unlock the STM32 RTC shadow registers. */
      HAL_RTC_GetTime(&hrtc,&nowtime,RTC_FORMAT_BIN);
      HAL_RTC_GetDate(&hrtc,&nowdate,RTC_FORMAT_BIN);

      if(EEPROM_UserDataLoad(&user_data, nowdate.Year, nowdate.Month, nowdate.Date))
      {
        user_data.wrist_enabled = 0U;
        user_data.app_sync_enabled = 0U;
        user_data.year = nowdate.Year;
        user_data.month = nowdate.Month;
        user_data.day = nowdate.Date;
        user_data.steps = 0U;
      }

      HWInterface.IMU.wrist_is_enabled = user_data.wrist_enabled;
      ui_APPSy_EN = user_data.app_sync_enabled;

      if(user_data.year == nowdate.Year &&
         user_data.month == nowdate.Month &&
         user_data.day == nowdate.Date)
      {
        if(!HWInterface.IMU.ConnectionError)
          HWInterface.IMU.SetSteps((unsigned long)user_data.steps);
        HWInterface.IMU.Steps = user_data.steps;
      }
      else
      {
        /* A different full date starts a new accounting day. */
        if(!HWInterface.IMU.ConnectionError)
          HWInterface.IMU.SetSteps(0UL);
        HWInterface.IMU.Steps = 0U;
        user_data.year = nowdate.Year;
        user_data.month = nowdate.Month;
        user_data.day = nowdate.Date;
        user_data.steps = 0U;
        (void)EEPROM_UserDataSave(&user_data);
      }
    }

    /* OTA metadata shares the AT24C02 but uses only the reserved 0x40-0xFF. */
    (void)ota_app_init();

    // BLE
    HWInterface.BLE.Init();
    HWInterface.BLE.Disable();

    // touch
    CST816_GPIO_Init();
    CST816_RESET();

    // lcd
    LCD_Init();
    LCD_Fill(0,0, LCD_W, LCD_H, RED);
    delay_ms(10);
    LCD_Set_Light(50);
    LCD_ShowString(72,LCD_H/2,(uint8_t*)"Welcome", WHITE, BLACK, 24, 0);//12*6,16*8,24*12,32*16
    uint8_t lcd_buf_str[17];
    sprintf(lcd_buf_str, "OV-Watch V%d.%d.%d", watch_version_major(), watch_version_minor(), watch_version_patch());
    LCD_ShowString(34, LCD_H/2+48, (uint8_t*)lcd_buf_str, WHITE, BLACK, 24, 0);
    delay_ms(1000);
    LCD_Fill(0, LCD_H/2-24, LCD_W, LCD_H/2+49, BLACK);


    // ui
    // LVGL init
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    ui_init();

    /* Reaching this point means the trial image completed hardware/UI init. */
    (void)ota_app_confirm_running_image();

    xTaskResumeAll();//恢复调度。
		vTaskDelete(NULL);//删除自己（任务完成一次后不再运行
		osDelay(500);
	}
}

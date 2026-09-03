#ifndef __DATASAVE_H__
#define __DATASAVE_H__

#include <stdint.h>

#ifndef USER_DATA_HOST_TEST
#include "BL24C02.h"
#else
void BL24C02_Init(void);
uint8_t BL24C02_WriteSafe(uint8_t addr, uint8_t length, const uint8_t buff[]);
uint8_t BL24C02_ReadSafe(uint8_t addr, uint8_t length, uint8_t buff[]);
#endif

#define USER_DATA_SLOT_A_ADDRESS 0x00U
#define USER_DATA_SLOT_B_ADDRESS 0x20U
#define USER_DATA_SLOT_SIZE      0x20U
#define USER_DATA_RECORD_SIZE    24U

typedef struct
{
	uint8_t wrist_enabled;
	uint8_t app_sync_enabled;
	uint8_t year;
	uint8_t month;
	uint8_t day;
	uint16_t steps;
} EEPROM_UserData_t;

void EEPROM_Init(void);
uint8_t EEPROM_UserDataLoad(EEPROM_UserData_t *data,
								uint8_t now_year, uint8_t now_month, uint8_t now_day);
uint8_t EEPROM_UserDataSave(const EEPROM_UserData_t *data);

#endif

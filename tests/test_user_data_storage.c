#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "DataSave.h"

static uint8_t eeprom[256];
static int writes_before_failure = -1;

void BL24C02_Init(void)
{
}

uint8_t BL24C02_WriteSafe(uint8_t address, uint8_t length, const uint8_t data[])
{
	if(data == NULL || length == 0U || (uint16_t)address + length > sizeof(eeprom))
		return 1U;
	if(writes_before_failure == 0)
		return 1U;
	if(writes_before_failure > 0)
		--writes_before_failure;
	memcpy(&eeprom[address], data, length);
	return 0U;
}

uint8_t BL24C02_ReadSafe(uint8_t address, uint8_t length, uint8_t data[])
{
	if(data == NULL || length == 0U || (uint16_t)address + length > sizeof(eeprom))
		return 1U;
	memcpy(data, &eeprom[address], length);
	return 0U;
}

static int same_user_data(const EEPROM_UserData_t *a, const EEPROM_UserData_t *b)
{
	return a->wrist_enabled == b->wrist_enabled &&
		a->app_sync_enabled == b->app_sync_enabled &&
		a->year == b->year && a->month == b->month && a->day == b->day &&
		a->steps == b->steps;
}

int main(void)
{
	EEPROM_UserData_t data;
	EEPROM_UserData_t loaded;
	EEPROM_UserData_t old_copy;

	/* Blank EEPROM defaults and immediately creates the first valid record. */
	memset(eeprom, 0xFF, sizeof(eeprom));
	if(EEPROM_UserDataLoad(&loaded, 26U, 9U, 1U) != 0U) return 1;
	if(loaded.wrist_enabled != 0U || loaded.app_sync_enabled != 0U ||
	   loaded.steps != 0U || loaded.year != 26U || loaded.month != 9U || loaded.day != 1U) return 2;

	/* A second save goes to the other slot and must win by sequence number. */
	data = loaded;
	data.wrist_enabled = 1U;
	data.app_sync_enabled = 1U;
	data.steps = 1234U;
	if(EEPROM_UserDataSave(&data) != 0U) return 3;
	if(EEPROM_UserDataLoad(&loaded, 26U, 9U, 1U) != 0U || !same_user_data(&data, &loaded)) return 4;

	/* Corrupt the newest slot: loading must fall back to the older valid slot. */
	eeprom[USER_DATA_SLOT_B_ADDRESS + 14U] ^= 0x40U;
	if(EEPROM_UserDataLoad(&old_copy, 26U, 9U, 1U) != 0U) return 5;
	if(old_copy.steps != 0U || old_copy.wrist_enabled != 0U) return 6;

	/* Simulate power loss during the target body write; the old slot survives. */
	data = old_copy;
	data.steps = 88U;
	writes_before_failure = 1;
	if(EEPROM_UserDataSave(&data) == 0U) return 7;
	writes_before_failure = -1;
	if(EEPROM_UserDataLoad(&loaded, 26U, 9U, 1U) != 0U || !same_user_data(&old_copy, &loaded)) return 8;

	/* Migrate the original sparse layout, including its big-endian step value. */
	memset(eeprom, 0xFF, sizeof(eeprom));
	eeprom[0x00] = 0x55U;
	eeprom[0x01] = 0xAAU;
	eeprom[0x10] = 1U;
	eeprom[0x11] = 0U;
	eeprom[0x20] = 1U;
	eeprom[0x21] = 0x12U;
	eeprom[0x22] = 0x34U;
	if(EEPROM_UserDataLoad(&loaded, 26U, 9U, 1U) != 0U) return 9;
	if(loaded.wrist_enabled != 1U || loaded.app_sync_enabled != 0U ||
	   loaded.steps != 0x1234U || loaded.year != 26U || loaded.month != 9U || loaded.day != 1U) return 10;

	puts("User data storage tests passed");
	return 0;
}

#include "BL24C02.h"

#define BL_CLK_ENABLE __HAL_RCC_GPIOA_CLK_ENABLE()
#define BL24C02_TOTAL_SIZE 256U
#define BL24C02_PAGE_SIZE  8U
#define BL24C02_WRITE_TIME 6U

iic_bus_t BL_bus = 
{
	.IIC_SDA_PORT = GPIOA,
	.IIC_SCL_PORT = GPIOA,
	.IIC_SDA_PIN  = GPIO_PIN_11,
	.IIC_SCL_PIN  = GPIO_PIN_12,
};


void BL24C02_Write(uint8_t addr,uint8_t length,uint8_t buff[])
{
	IIC_Write_Multi_Byte(&BL_bus, BL_ADDRESS, addr, length, buff);
}


void BL24C02_Read(uint8_t addr, uint8_t length, uint8_t buff[])
{
	IIC_Read_Multi_Byte(&BL_bus, BL_ADDRESS, addr, length, buff);
}


/*
 * AT24C02 page writes wrap inside an 8-byte page. Split longer records at
 * page boundaries and wait for the EEPROM's internal programming cycle.
 */
uint8_t BL24C02_WriteSafe(uint8_t addr, uint8_t length, const uint8_t buff[])
{
	uint16_t end = (uint16_t)addr + length;
	uint8_t remaining = length;

	if(buff == NULL || length == 0U || end > BL24C02_TOTAL_SIZE)
		return 1U;

	while(remaining > 0U)
	{
		uint8_t page_left = BL24C02_PAGE_SIZE - (addr % BL24C02_PAGE_SIZE);
		uint8_t count = (remaining < page_left) ? remaining : page_left;

		if(IIC_Write_Multi_Byte(&BL_bus, BL_ADDRESS, addr, count, (uint8_t *)buff))
			return 1U;

		delay_ms(BL24C02_WRITE_TIME);
		addr = (uint8_t)(addr + count);
		buff += count;
		remaining = (uint8_t)(remaining - count);
	}

	return 0U;
}


uint8_t BL24C02_ReadSafe(uint8_t addr, uint8_t length, uint8_t buff[])
{
	uint16_t end = (uint16_t)addr + length;

	if(buff == NULL || length == 0U || end > BL24C02_TOTAL_SIZE)
		return 1U;

	return IIC_Read_Multi_Byte(&BL_bus, BL_ADDRESS, addr, length, buff);
}


void BL24C02_Init(void)
{
	BL_CLK_ENABLE;
	IICInit(&BL_bus);
}

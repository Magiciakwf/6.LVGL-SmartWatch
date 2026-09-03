#include "ota_board.h"
#include "ota_config.h"
#include <string.h>

#define W25_CMD_WRITE_ENABLE  0x06U
#define W25_CMD_READ_STATUS1  0x05U
#define W25_CMD_READ_DATA     0x03U
#define W25_CMD_PAGE_PROGRAM  0x02U
#define W25_CMD_SECTOR_ERASE  0x20U
#define W25_CMD_BLOCK_ERASE64 0xD8U
#define W25_CMD_JEDEC_ID      0x9FU
#define W25_CMD_RELEASE_PD    0xABU

static void bus_delay(void)
{
    volatile uint32_t count;
    for (count = 0U; count < 12U; ++count) {
        __NOP();
    }
}

static void gpio_clocks_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
}

static void i2c_sda(uint8_t high)
{
    HAL_GPIO_WritePin(OTA_AT24_SDA_PORT, OTA_AT24_SDA_PIN,
                      high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void i2c_scl(uint8_t high)
{
    HAL_GPIO_WritePin(OTA_AT24_SCL_PORT, OTA_AT24_SCL_PIN,
                      high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void i2c_start(void)
{
    i2c_sda(1U); i2c_scl(1U); bus_delay();
    i2c_sda(0U); bus_delay(); i2c_scl(0U); bus_delay();
}

static void i2c_stop(void)
{
    i2c_sda(0U); bus_delay(); i2c_scl(1U); bus_delay();
    i2c_sda(1U); bus_delay();
}

static int i2c_write_byte(uint8_t value)
{
    uint32_t bit;
    GPIO_PinState ack;
    for (bit = 0U; bit < 8U; ++bit) {
        i2c_sda((value & 0x80U) != 0U); bus_delay();
        i2c_scl(1U); bus_delay(); i2c_scl(0U); bus_delay();
        value <<= 1U;
    }
    i2c_sda(1U); bus_delay(); i2c_scl(1U); bus_delay();
    ack = HAL_GPIO_ReadPin(OTA_AT24_SDA_PORT, OTA_AT24_SDA_PIN);
    i2c_scl(0U); bus_delay();
    return (ack == GPIO_PIN_RESET) ? 0 : -1;
}

static uint8_t i2c_read_byte(uint8_t acknowledge)
{
    uint32_t bit;
    uint8_t value = 0U;
    i2c_sda(1U);
    for (bit = 0U; bit < 8U; ++bit) {
        value <<= 1U;
        i2c_scl(1U); bus_delay();
        if (HAL_GPIO_ReadPin(OTA_AT24_SDA_PORT, OTA_AT24_SDA_PIN) == GPIO_PIN_SET) {
            value |= 1U;
        }
        i2c_scl(0U); bus_delay();
    }
    i2c_sda(acknowledge ? 0U : 1U); bus_delay();
    i2c_scl(1U); bus_delay(); i2c_scl(0U); bus_delay();
    i2c_sda(1U);
    return value;
}

static int at24_ready(void)
{
    uint32_t attempt;
    for (attempt = 0U; attempt < 20U; ++attempt) {
        i2c_start();
        if (i2c_write_byte((uint8_t)(OTA_AT24_I2C_ADDRESS << 1U)) == 0) {
            i2c_stop();
            return 0;
        }
        i2c_stop();
        HAL_Delay(1U);
    }
    return -1;
}

int ota_at24_read(uint8_t address, void *data, uint16_t length)
{
    uint8_t *output = (uint8_t *)data;
    uint16_t i;
    if ((data == NULL) || (length == 0U) || (((uint16_t)address + length) > 256U)) {
        return -1;
    }
    i2c_start();
    if ((i2c_write_byte((uint8_t)(OTA_AT24_I2C_ADDRESS << 1U)) != 0) ||
        (i2c_write_byte(address) != 0)) {
        i2c_stop();
        return -1;
    }
    i2c_start();
    if (i2c_write_byte((uint8_t)((OTA_AT24_I2C_ADDRESS << 1U) | 1U)) != 0) {
        i2c_stop();
        return -1;
    }
    for (i = 0U; i < length; ++i) {
        output[i] = i2c_read_byte((uint8_t)(i + 1U < length));
    }
    i2c_stop();
    return 0;
}

int ota_at24_write(uint8_t address, const void *data, uint16_t length)
{
    const uint8_t *input = (const uint8_t *)data;
    uint16_t remaining = length;
    if ((data == NULL) || (length == 0U) || (((uint16_t)address + length) > 256U)) {
        return -1;
    }
    while (remaining != 0U) {
        uint16_t page_left = OTA_AT24_PAGE_SIZE - (address % OTA_AT24_PAGE_SIZE);
        uint16_t count = (remaining < page_left) ? remaining : page_left;
        uint16_t i;
        i2c_start();
        if ((i2c_write_byte((uint8_t)(OTA_AT24_I2C_ADDRESS << 1U)) != 0) ||
            (i2c_write_byte(address) != 0)) {
            i2c_stop();
            return -1;
        }
        for (i = 0U; i < count; ++i) {
            if (i2c_write_byte(input[i]) != 0) {
                i2c_stop();
                return -1;
            }
        }
        i2c_stop();
        if (at24_ready() != 0) {
            return -1;
        }
        address = (uint8_t)(address + count);
        input += count;
        remaining = (uint16_t)(remaining - count);
    }
    return 0;
}

static void w25_cs(uint8_t high)
{
    HAL_GPIO_WritePin(OTA_W25_CS_PORT, OTA_W25_CS_PIN,
                      high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint8_t spi_transfer(uint8_t output)
{
    uint32_t bit;
    uint8_t input = 0U;
    for (bit = 0U; bit < 8U; ++bit) {
        HAL_GPIO_WritePin(OTA_W25_MOSI_PORT, OTA_W25_MOSI_PIN,
                          (output & 0x80U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        output <<= 1U;
        bus_delay();
        HAL_GPIO_WritePin(OTA_W25_SCK_PORT, OTA_W25_SCK_PIN, GPIO_PIN_SET);
        bus_delay();
        input <<= 1U;
        if (HAL_GPIO_ReadPin(OTA_W25_MISO_PORT, OTA_W25_MISO_PIN) == GPIO_PIN_SET) {
            input |= 1U;
        }
        HAL_GPIO_WritePin(OTA_W25_SCK_PORT, OTA_W25_SCK_PIN, GPIO_PIN_RESET);
    }
    return input;
}

static void w25_command_address(uint8_t command, uint32_t address)
{
    spi_transfer(command);
    spi_transfer((uint8_t)(address >> 16U));
    spi_transfer((uint8_t)(address >> 8U));
    spi_transfer((uint8_t)address);
}

static int w25_wait_ready(uint32_t timeout_ms)
{
    uint32_t started = HAL_GetTick();
    uint8_t status;
    do {
        w25_cs(0U);
        spi_transfer(W25_CMD_READ_STATUS1);
        status = spi_transfer(0xFFU);
        w25_cs(1U);
        if ((status & 1U) == 0U) {
            return 0;
        }
    } while ((HAL_GetTick() - started) < timeout_ms);
    return -1;
}

static int w25_write_enable(void)
{
    if (w25_wait_ready(1000U) != 0) {
        return -1;
    }
    w25_cs(0U); spi_transfer(W25_CMD_WRITE_ENABLE); w25_cs(1U);
    return 0;
}

int ota_w25_init(void)
{
    uint8_t manufacturer;
    uint8_t memory_type;
    uint8_t capacity;
    w25_cs(0U); spi_transfer(W25_CMD_RELEASE_PD); w25_cs(1U);
    HAL_Delay(1U);
    w25_cs(0U);
    spi_transfer(W25_CMD_JEDEC_ID);
    manufacturer = spi_transfer(0xFFU);
    memory_type = spi_transfer(0xFFU);
    capacity = spi_transfer(0xFFU);
    w25_cs(1U);
    (void)memory_type;
    return ((manufacturer == 0xEFU) && (capacity == 0x17U)) ? 0 : -1;
}

int ota_w25_read(uint32_t address, void *data, uint32_t length)
{
    uint8_t *output = (uint8_t *)data;
    uint32_t i;
    if ((data == NULL) || ((address + length) > OTA_W25_TOTAL_SIZE)) {
        return -1;
    }
    if (w25_wait_ready(1000U) != 0) {
        return -1;
    }
    w25_cs(0U);
    w25_command_address(W25_CMD_READ_DATA, address);
    for (i = 0U; i < length; ++i) {
        output[i] = spi_transfer(0xFFU);
    }
    w25_cs(1U);
    return 0;
}

int ota_w25_write(uint32_t address, const void *data, uint32_t length)
{
    const uint8_t *input = (const uint8_t *)data;
    if ((data == NULL) || ((address + length) > OTA_W25_TOTAL_SIZE)) {
        return -1;
    }
    while (length != 0U) {
        uint32_t page_left = 256U - (address & 0xFFU);
        uint32_t count = (length < page_left) ? length : page_left;
        uint32_t i;
        if (w25_write_enable() != 0) {
            return -1;
        }
        w25_cs(0U);
        w25_command_address(W25_CMD_PAGE_PROGRAM, address);
        for (i = 0U; i < count; ++i) {
            spi_transfer(input[i]);
        }
        w25_cs(1U);
        if (w25_wait_ready(1000U) != 0) {
            return -1;
        }
        address += count;
        input += count;
        length -= count;
    }
    return 0;
}

int ota_w25_erase_range(uint32_t address, uint32_t length)
{
    uint32_t end;
    if ((length == 0U) || ((address + length) > OTA_W25_TOTAL_SIZE)) {
        return -1;
    }
    end = (address + length + 0xFFFUL) & ~0xFFFUL;
    address &= ~0xFFFUL;
    while (address < end) {
        uint8_t command;
        uint32_t step;
        if (((address & 0xFFFFUL) == 0U) && ((end - address) >= 0x10000UL)) {
            command = W25_CMD_BLOCK_ERASE64;
            step = 0x10000UL;
        } else {
            command = W25_CMD_SECTOR_ERASE;
            step = 0x1000UL;
        }
        if (w25_write_enable() != 0) {
            return -1;
        }
        w25_cs(0U); w25_command_address(command, address); w25_cs(1U);
        if (w25_wait_ready(5000U) != 0) {
            return -1;
        }
        address += step;
    }
    return 0;
}

int ota_board_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio_clocks_init();

    gpio.Pin = OTA_AT24_SCL_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(OTA_AT24_SCL_PORT, &gpio);
    gpio.Pin = OTA_AT24_SDA_PIN;
    HAL_GPIO_Init(OTA_AT24_SDA_PORT, &gpio);
    i2c_sda(1U); i2c_scl(1U);

    gpio.Pin = OTA_W25_SCK_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(OTA_W25_SCK_PORT, &gpio);
    gpio.Pin = OTA_W25_MOSI_PIN;
    HAL_GPIO_Init(OTA_W25_MOSI_PORT, &gpio);
    gpio.Pin = OTA_W25_CS_PIN;
    HAL_GPIO_Init(OTA_W25_CS_PORT, &gpio);
    gpio.Pin = OTA_W25_MISO_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(OTA_W25_MISO_PORT, &gpio);
    HAL_GPIO_WritePin(OTA_W25_SCK_PORT, OTA_W25_SCK_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(OTA_W25_MOSI_PORT, OTA_W25_MOSI_PIN, GPIO_PIN_RESET);
    w25_cs(1U);
    return ota_w25_init();
}

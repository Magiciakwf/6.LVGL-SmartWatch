#ifndef OTA_CONFIG_H
#define OTA_CONFIG_H

#ifndef OTA_HOST_TEST
#include "stm32f4xx_hal.h"
#endif

/* STM32F411CEU6 internal flash layout.  Sectors 0..3 are the bootloader. */
#define OTA_BOOT_BASE_ADDRESS        0x08000000UL//bootLoader起始地址
#define OTA_BOOT_SIZE                0x00010000UL
#define OTA_APP_BASE_ADDRESS         0x08010000UL//APP起始地址
#define OTA_APP_MAX_SIZE             0x00070000UL
#define OTA_SRAM_BASE_ADDRESS        0x20000000UL
#define OTA_SRAM_END_ADDRESS         0x20020000UL

/* W25Q64: two 1 MiB image slots.  The remaining 6 MiB is left to the app. */
#define OTA_W25_TOTAL_SIZE           0x00800000UL
#define OTA_SLOT_SIZE                0x00100000UL//1MB
#define OTA_SLOT_A_ADDRESS           0x00000000UL//A区起始地址
#define OTA_SLOT_B_ADDRESS           0x00100000UL//B区起始地址

/* The first 64 bytes of AT24C02 are reserved for the user-data A/B records. */
#define OTA_META_COPY0_ADDRESS       0x40U
#define OTA_META_COPY1_ADDRESS       0xA0U
#define OTA_META_RECORD_SIZE         96U

#define OTA_PACKAGE_HEADER_SIZE      256U
#define OTA_PACKAGE_KEY_ID           1UL
#define OTA_MAX_TRIAL_BOOTS          3U

/*
 * Default board wiring for the OTA memories.
 * W25Q64 uses a dedicated software-SPI bus so it does not collide with the
 * ST7789 on hardware SPI1.  Override these macros in the Keil target if the
 * PCB uses different pins.
 *
 *   W25Q64 SCK=PA5, MISO=PA6, MOSI=PA7, CS=PB10
 *   AT24C02 SCL=PA12, SDA=PA11 (matches the existing BL24C02 driver)
 */
#ifndef OTA_W25_SCK_PORT
#define OTA_W25_SCK_PORT             GPIOA
#define OTA_W25_SCK_PIN              GPIO_PIN_5
#define OTA_W25_MISO_PORT            GPIOA
#define OTA_W25_MISO_PIN             GPIO_PIN_6
#define OTA_W25_MOSI_PORT            GPIOA
#define OTA_W25_MOSI_PIN             GPIO_PIN_7
#define OTA_W25_CS_PORT              GPIOB
#define OTA_W25_CS_PIN               GPIO_PIN_10
#endif

#ifndef OTA_AT24_SCL_PORT
#define OTA_AT24_SCL_PORT             GPIOA
#define OTA_AT24_SCL_PIN              GPIO_PIN_12
#define OTA_AT24_SDA_PORT             GPIOA
#define OTA_AT24_SDA_PIN              GPIO_PIN_11
#endif

#define OTA_AT24_I2C_ADDRESS          0x50U
#define OTA_AT24_PAGE_SIZE            8U

/*
 * DEVELOPMENT KEY ONLY.  Replace this 128-bit key for production and pass the
 * same key to tools/ota_pack.py.  Production keys should be injected by the
 * build system instead of committed to source control.
 */
#ifndef OTA_AES128_KEY_BYTES
#define OTA_AES128_KEY_BYTES \
  {0x2BU,0x7EU,0x15U,0x16U,0x28U,0xAEU,0xD2U,0xA6U, \
   0xABU,0xF7U,0x15U,0x88U,0x09U,0xCFU,0x4FU,0x3CU}
#endif

#endif /* OTA_CONFIG_H */

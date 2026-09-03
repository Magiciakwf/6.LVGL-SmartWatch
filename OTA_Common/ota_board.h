#ifndef OTA_BOARD_H
#define OTA_BOARD_H

#include <stdint.h>

int ota_board_init(void);

int ota_at24_read(uint8_t address, void *data, uint16_t length);
int ota_at24_write(uint8_t address, const void *data, uint16_t length);

int ota_w25_init(void);
int ota_w25_read(uint32_t address, void *data, uint32_t length);
int ota_w25_write(uint32_t address, const void *data, uint32_t length);
int ota_w25_erase_range(uint32_t address, uint32_t length);

#endif

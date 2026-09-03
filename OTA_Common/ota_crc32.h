#ifndef OTA_CRC32_H
#define OTA_CRC32_H

#include <stdint.h>

uint32_t ota_crc32_init(void);
uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, uint32_t length);
uint32_t ota_crc32_final(uint32_t crc);
uint32_t ota_crc32(const void *data, uint32_t length);

#endif

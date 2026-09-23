#include "ota_crc32.h"

uint32_t ota_crc32_init(void)
{
    return 0xFFFFFFFFUL;
}

uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
    uint32_t i;
    uint32_t bit;

    for (i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
        }
    }
    return crc;
}

uint32_t ota_crc32_final(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFUL;
}

uint32_t ota_crc32(const void *data, uint32_t length)
{
    return ota_crc32_final(ota_crc32_update(ota_crc32_init(),
                                             (const uint8_t *)data,
                                             length));
}





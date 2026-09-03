#ifndef OTA_AES_H
#define OTA_AES_H

#include <stdint.h>

typedef struct {
    uint8_t round_key[176];
    uint8_t counter[16];
    uint8_t stream[16];
    uint8_t stream_used;
} ota_aes128_ctr_context_t;

void ota_aes128_ctr_init(ota_aes128_ctr_context_t *context,
                         const uint8_t key[16], const uint8_t iv[16]);
void ota_aes128_ctr_crypt(ota_aes128_ctr_context_t *context,
                          uint8_t *data, uint32_t length);
const uint8_t *ota_aes128_default_key(void);

#endif

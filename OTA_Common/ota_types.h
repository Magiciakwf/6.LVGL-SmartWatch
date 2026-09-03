#ifndef OTA_TYPES_H
#define OTA_TYPES_H

#include <stdint.h>
#include "ota_config.h"

#if defined(__CC_ARM)
#define OTA_PACKED __attribute__((packed))
#else
#define OTA_PACKED __attribute__((packed))
#endif

#define OTA_META_MAGIC               0x4F54414DUL /* OTAM */
#define OTA_META_FORMAT_VERSION      2U
#define OTA_PACKAGE_MAGIC            0x4F544131UL /* OTA1 */
#define OTA_PACKAGE_FORMAT_VERSION   2U

#define OTA_PACKAGE_FLAG_AES128_CTR  (1UL << 0)
#define OTA_META_FLAG_ACTIVE_VALID   (1UL << 0)
#define OTA_META_FLAG_PREVIOUS_VALID (1UL << 1)


typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_PENDING,
    OTA_STATE_INSTALLING,
    OTA_STATE_TESTING,
    OTA_STATE_CONFIRMED,
    OTA_STATE_ROLLBACK,
    OTA_STATE_ERROR
} ota_state_t;

typedef enum {
    OTA_ERROR_NONE = 0,
    OTA_ERROR_STORAGE_INIT,
    OTA_ERROR_NO_UPDATE,
    OTA_ERROR_DOWNLOAD,
    OTA_ERROR_PACKAGE_SIZE,
    OTA_ERROR_HEADER,
    OTA_ERROR_PAYLOAD_CRC,
    OTA_ERROR_FLASH_ERASE,
    OTA_ERROR_FLASH_PROGRAM,
    OTA_ERROR_PLAIN_CRC,
    OTA_ERROR_VECTOR,
    OTA_ERROR_NO_ROLLBACK
} ota_error_t;

typedef struct OTA_PACKED {
    uint32_t magic;
    uint16_t format_version;
    uint16_t record_size;
    uint32_t sequence;
    uint32_t record_crc32;
    uint8_t state;
    uint8_t active_slot;
    uint8_t pending_slot;
    uint8_t previous_slot;
    uint32_t flags;
    uint8_t trial_boots;
    uint8_t max_trial_boots;
    uint16_t last_error;
    uint32_t active_version;
    uint32_t pending_version;
    uint32_t pending_package_size;
    uint32_t downloaded_size;
    uint32_t pending_task_id;
    uint8_t reserved[48];
} ota_metadata_t;

typedef struct OTA_PACKED {
    uint32_t magic;
    uint16_t format_version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t firmware_version;
    uint32_t app_load_address;
    uint32_t image_size;
    uint32_t payload_size;
    uint32_t plain_crc32;
    uint32_t payload_crc32;
    uint8_t aes_iv[16];
    uint32_t key_id;
    uint32_t header_crc32;
    uint8_t reserved[196];
} ota_package_header_t;

typedef char ota_metadata_size_check[(sizeof(ota_metadata_t) == OTA_META_RECORD_SIZE) ? 1 : -1];
typedef char ota_header_size_check[(sizeof(ota_package_header_t) == OTA_PACKAGE_HEADER_SIZE) ? 1 : -1];

static inline uint32_t ota_slot_address(uint8_t slot)
{
    return (slot == 0U) ? OTA_SLOT_A_ADDRESS : OTA_SLOT_B_ADDRESS;
}

#endif /* OTA_TYPES_H */

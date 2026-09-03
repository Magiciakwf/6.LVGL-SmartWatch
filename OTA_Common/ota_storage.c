#include "ota_storage.h"
#include "ota_board.h"
#include "ota_crc32.h"
#include <string.h>

static uint32_t metadata_crc(const ota_metadata_t *metadata)
{
    ota_metadata_t copy;
    memcpy(&copy, metadata, sizeof(copy));
    copy.record_crc32 = 0U;
    return ota_crc32(&copy, sizeof(copy));
}

static int metadata_valid(const ota_metadata_t *metadata)
{
    return (metadata->magic == OTA_META_MAGIC) &&
           (metadata->format_version == OTA_META_FORMAT_VERSION) &&
           (metadata->record_size == sizeof(*metadata)) &&
           (metadata->state <= OTA_STATE_ERROR) &&
           (metadata->active_slot <= 1U) &&
           (metadata->pending_slot <= 1U) &&
           (metadata->previous_slot <= 1U) &&
           (metadata->record_crc32 == metadata_crc(metadata));
}

void ota_metadata_default(ota_metadata_t *metadata)
{
    memset(metadata, 0, sizeof(*metadata));
    metadata->magic = OTA_META_MAGIC;
    metadata->format_version = OTA_META_FORMAT_VERSION;
    metadata->record_size = sizeof(*metadata);
    metadata->state = OTA_STATE_IDLE;
    metadata->active_slot = 0U;
    metadata->pending_slot = 1U;
    metadata->previous_slot = 0U;
    metadata->max_trial_boots = OTA_MAX_TRIAL_BOOTS;
}

int ota_metadata_load(ota_metadata_t *metadata)
{
    ota_metadata_t first;
    ota_metadata_t second;
    int first_ok;
    int second_ok;
    if (metadata == NULL) {
        return -1;
    }
    first_ok = (ota_at24_read(OTA_META_COPY0_ADDRESS, &first, sizeof(first)) == 0) &&
               metadata_valid(&first);
    second_ok = (ota_at24_read(OTA_META_COPY1_ADDRESS, &second, sizeof(second)) == 0) &&
                metadata_valid(&second);
    if (!first_ok && !second_ok) {
        ota_metadata_default(metadata);
        return 1;
    }
    if (first_ok && second_ok) {
        *metadata = ((int32_t)(second.sequence - first.sequence) > 0) ? second : first;
    } else {
        *metadata = first_ok ? first : second;
    }
    return 0;
}

int ota_metadata_store(ota_metadata_t *metadata)
{
    ota_metadata_t verify;
    uint8_t target;
    if (metadata == NULL) {
        return -1;
    }
    metadata->magic = OTA_META_MAGIC;
    metadata->format_version = OTA_META_FORMAT_VERSION;
    metadata->record_size = sizeof(*metadata);
    metadata->sequence++;
    metadata->record_crc32 = 0U;
    metadata->record_crc32 = metadata_crc(metadata);
    target = ((metadata->sequence & 1U) != 0U) ? OTA_META_COPY1_ADDRESS : OTA_META_COPY0_ADDRESS;
    if ((ota_at24_write(target, metadata, sizeof(*metadata)) != 0) ||
        (ota_at24_read(target, &verify, sizeof(verify)) != 0) ||
        !metadata_valid(&verify) || (verify.sequence != metadata->sequence)) {
        return -1;
    }
    return 0;
}

uint32_t ota_package_header_crc(const ota_package_header_t *header)
{
    ota_package_header_t copy;
    memcpy(&copy, header, sizeof(copy));
    copy.header_crc32 = 0U;
    return ota_crc32(&copy, sizeof(copy));
}

int ota_package_header_valid(const ota_package_header_t *header)
{
    uint32_t package_size;
    if ((header == NULL) ||
        (header->magic != OTA_PACKAGE_MAGIC) ||
        (header->format_version != OTA_PACKAGE_FORMAT_VERSION) ||
        (header->header_size != sizeof(*header)) ||
        (header->app_load_address != OTA_APP_BASE_ADDRESS) ||
        (header->image_size == 0U) ||
        (header->image_size > OTA_APP_MAX_SIZE) ||
        (header->payload_size != header->image_size) ||
        ((header->flags & ~OTA_PACKAGE_FLAG_AES128_CTR) != 0U) ||
        (((header->flags & OTA_PACKAGE_FLAG_AES128_CTR) != 0U) &&
         (header->key_id != OTA_PACKAGE_KEY_ID)) ||
        (header->header_crc32 != ota_package_header_crc(header))) {
        return 0;
    }
    package_size = header->header_size + header->payload_size;
    return (package_size <= OTA_SLOT_SIZE) ? 1 : 0;
}

int ota_package_read_header(uint8_t slot, ota_package_header_t *header)
{
    if ((slot > 1U) || (header == NULL) ||
        (ota_w25_read(ota_slot_address(slot), header, sizeof(*header)) != 0)) {
        return -1;
    }
    return ota_package_header_valid(header) ? 0 : -1;
}

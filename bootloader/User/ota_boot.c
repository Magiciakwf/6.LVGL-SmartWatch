#include "ota_boot.h"
#include "ota_board.h"
#include "ota_storage.h"
#include "ota_crc32.h"
#include "ota_aes.h"
#include "stm32f4xx_hal.h"
#include <string.h>

#define OTA_IO_BUFFER_SIZE 256U

typedef void (*ota_app_entry_t)(void);

static int crc_w25(uint32_t address, uint32_t length, uint32_t *crc_result)
{
    uint8_t buffer[OTA_IO_BUFFER_SIZE];
    uint32_t crc = ota_crc32_init();
    uint32_t count;
    while (length != 0U) {
        count = (length > sizeof(buffer)) ? sizeof(buffer) : length;
        if (ota_w25_read(address, buffer, count) != 0) {
            return -1;
        }
        crc = ota_crc32_update(crc, buffer, count);
        address += count;
        length -= count;
    }
    *crc_result = ota_crc32_final(crc);
    return 0;
}

static int package_valid(uint8_t slot, const ota_metadata_t *metadata,
                         uint8_t is_pending, ota_package_header_t *header)
{
    uint32_t crc;
    uint32_t base = ota_slot_address(slot);
    uint32_t package_size;

    if (ota_package_read_header(slot, header) != 0) {
        return -1;
    }
    package_size = header->header_size + header->payload_size;
    if (is_pending != 0U) {
        if ((metadata->pending_package_size != package_size) ||
            (crc_w25(base, package_size, &crc) != 0)) {
            return -1;
        }
    }
    if ((crc_w25(base + header->header_size, header->payload_size, &crc) != 0) ||
        (crc != header->payload_crc32)) {
        return -1;
    }
    return 0;
}

static int internal_app_valid(void)
{
    uint32_t stack = *(volatile uint32_t *)OTA_APP_BASE_ADDRESS;
    uint32_t reset = *(volatile uint32_t *)(OTA_APP_BASE_ADDRESS + 4U);
    return (stack >= OTA_SRAM_BASE_ADDRESS) && (stack <= OTA_SRAM_END_ADDRESS) &&
           ((reset & 1U) != 0U) &&
           ((reset & ~1UL) >= OTA_APP_BASE_ADDRESS) &&
           ((reset & ~1UL) < (OTA_APP_BASE_ADDRESS + OTA_APP_MAX_SIZE));
}

static int decrypted_vector_valid(uint8_t slot, const ota_package_header_t *header)
{
    uint8_t vector[8];
    uint32_t stack;
    uint32_t reset;
    ota_aes128_ctr_context_t aes;
    if (ota_w25_read(ota_slot_address(slot) + header->header_size,
                     vector, sizeof(vector)) != 0) {
        return 0;
    }
    if ((header->flags & OTA_PACKAGE_FLAG_AES128_CTR) != 0U) {
        ota_aes128_ctr_init(&aes, ota_aes128_default_key(), header->aes_iv);
        ota_aes128_ctr_crypt(&aes, vector, sizeof(vector));
    }
    stack = ((uint32_t)vector[0]) | ((uint32_t)vector[1] << 8U) |
            ((uint32_t)vector[2] << 16U) | ((uint32_t)vector[3] << 24U);
    reset = ((uint32_t)vector[4]) | ((uint32_t)vector[5] << 8U) |
            ((uint32_t)vector[6] << 16U) | ((uint32_t)vector[7] << 24U);
    return (stack >= OTA_SRAM_BASE_ADDRESS) && (stack <= OTA_SRAM_END_ADDRESS) &&
           ((reset & 1U) != 0U) && ((reset & ~1UL) >= OTA_APP_BASE_ADDRESS) &&
           ((reset & ~1UL) < (OTA_APP_BASE_ADDRESS + OTA_APP_MAX_SIZE));
}

static int internal_erase(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0U;
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector = FLASH_SECTOR_4;
    erase.NbSectors = 4U;
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return -1;
    }
    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK) {
        HAL_FLASH_Lock();
        return -1;
    }
    return 0;
}

static int internal_program(uint32_t address, const uint8_t *data, uint32_t length)
{
    uint32_t offset;
    for (offset = 0U; offset < length; offset += 4U) {
        uint32_t word = 0xFFFFFFFFUL;
        uint32_t remaining = length - offset;
        uint32_t count = (remaining < 4U) ? remaining : 4U;
        memcpy(&word, &data[offset], count);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + offset, word) != HAL_OK) {
            return -1;
        }
    }
    return 0;
}

static int install_package(uint8_t slot, const ota_package_header_t *header,
                           ota_error_t *error)
{
    uint8_t buffer[OTA_IO_BUFFER_SIZE];
    ota_aes128_ctr_context_t aes;
    uint32_t crc = ota_crc32_init();
    uint32_t source = ota_slot_address(slot) + header->header_size;
    uint32_t destination = OTA_APP_BASE_ADDRESS;
    uint32_t remaining = header->image_size;
    uint32_t count;

    if (!decrypted_vector_valid(slot, header)) {
        *error = OTA_ERROR_VECTOR;
        return -1;
    }
    if (internal_erase() != 0) {
        *error = OTA_ERROR_FLASH_ERASE;
        return -1;
    }
    if ((header->flags & OTA_PACKAGE_FLAG_AES128_CTR) != 0U) {
        ota_aes128_ctr_init(&aes, ota_aes128_default_key(), header->aes_iv);
    }
    while (remaining != 0U) {
        count = (remaining > sizeof(buffer)) ? sizeof(buffer) : remaining;
        if (ota_w25_read(source, buffer, count) != 0) {
            *error = OTA_ERROR_STORAGE_INIT;
            HAL_FLASH_Lock();
            return -1;
        }
        if ((header->flags & OTA_PACKAGE_FLAG_AES128_CTR) != 0U) {
            ota_aes128_ctr_crypt(&aes, buffer, count);
        }
        crc = ota_crc32_update(crc, buffer, count);
        if (internal_program(destination, buffer, count) != 0) {
            *error = OTA_ERROR_FLASH_PROGRAM;
            HAL_FLASH_Lock();
            return -1;
        }
        source += count;
        destination += count;
        remaining -= count;
    }
    HAL_FLASH_Lock();
    crc = ota_crc32_final(crc);
    if (crc != header->plain_crc32) {
        *error = OTA_ERROR_PLAIN_CRC;
        return -1;
    }
    if (!internal_app_valid()) {
        *error = OTA_ERROR_VECTOR;
        return -1;
    }
    /* Verify the bytes actually programmed into internal flash, not only the
       decrypted RAM buffer used as the programming source. */
    crc = ota_crc32((const void *)OTA_APP_BASE_ADDRESS, header->image_size);
    if (crc != header->plain_crc32) {
        *error = OTA_ERROR_PLAIN_CRC;
        return -1;
    }
    return 0;
}

static int backup_internal_app(uint8_t slot, uint32_t version)
{
    ota_package_header_t header;
    uint8_t buffer[OTA_IO_BUFFER_SIZE];
    uint32_t crc = ota_crc32_init();
    uint32_t offset;
    uint32_t slot_base = ota_slot_address(slot);

    if (!internal_app_valid() ||
        (ota_w25_erase_range(slot_base, OTA_SLOT_SIZE) != 0)) {
        return -1;
    }
    for (offset = 0U; offset < OTA_APP_MAX_SIZE; offset += sizeof(buffer)) {
        memcpy(buffer, (const void *)(OTA_APP_BASE_ADDRESS + offset), sizeof(buffer));
        crc = ota_crc32_update(crc, buffer, sizeof(buffer));
        if (ota_w25_write(slot_base + OTA_PACKAGE_HEADER_SIZE + offset,
                          buffer, sizeof(buffer)) != 0) {
            return -1;
        }
    }
    crc = ota_crc32_final(crc);
    memset(&header, 0, sizeof(header));
    header.magic = OTA_PACKAGE_MAGIC;
    header.format_version = OTA_PACKAGE_FORMAT_VERSION;
    header.header_size = sizeof(header);
    header.firmware_version = version;
    header.app_load_address = OTA_APP_BASE_ADDRESS;
    header.image_size = OTA_APP_MAX_SIZE;
    header.payload_size = OTA_APP_MAX_SIZE;
    header.plain_crc32 = crc;
    header.payload_crc32 = crc;
    header.key_id = OTA_PACKAGE_KEY_ID;
    header.header_crc32 = ota_package_header_crc(&header);
    return ota_w25_write(slot_base, &header, sizeof(header));
}

static void jump_to_app(void)
{
    uint32_t stack;
    uint32_t reset;
    ota_app_entry_t entry;
    uint32_t i;
    if (!internal_app_valid()) {
        return;
    }
    stack = *(volatile uint32_t *)OTA_APP_BASE_ADDRESS;
    reset = *(volatile uint32_t *)(OTA_APP_BASE_ADDRESS + 4U);
    entry = (ota_app_entry_t)reset;
    HAL_DeInit();
    HAL_RCC_DeInit();
    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    for (i = 0U; i < 8U; ++i) {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }
    SCB->VTOR = OTA_APP_BASE_ADDRESS;
    __DSB();
    __ISB();
    __set_MSP(stack);
    entry();
}

static void store_error(ota_metadata_t *metadata, ota_error_t error)
{
    metadata->state = OTA_STATE_ERROR;
    metadata->last_error = (uint16_t)error;
    (void)ota_metadata_store(metadata);
}

static int rollback(ota_metadata_t *metadata)
{
    ota_package_header_t header;
    ota_error_t error = OTA_ERROR_NONE;
    if (((metadata->flags & OTA_META_FLAG_PREVIOUS_VALID) == 0U) ||
        (package_valid(metadata->previous_slot, metadata, 0U, &header) != 0)) {
        store_error(metadata, OTA_ERROR_NO_ROLLBACK);
        return -1;
    }
    metadata->state = OTA_STATE_ROLLBACK;
    (void)ota_metadata_store(metadata);
    if (install_package(metadata->previous_slot, &header, &error) != 0) {
        store_error(metadata, error);
        return -1;
    }
    metadata->active_slot = metadata->previous_slot;
    metadata->active_version = header.firmware_version;
    metadata->flags |= OTA_META_FLAG_ACTIVE_VALID;
    metadata->flags &= ~OTA_META_FLAG_PREVIOUS_VALID;
    metadata->trial_boots = 0U;
    metadata->state = OTA_STATE_IDLE;
    metadata->last_error = OTA_ERROR_NONE;
    (void)ota_metadata_store(metadata);
    return 0;
}

static void install_pending(ota_metadata_t *metadata)
{
    ota_package_header_t header;
    ota_package_header_t active_header;
    ota_error_t error = OTA_ERROR_NONE;
    uint8_t old_active = metadata->active_slot;

    metadata->state = OTA_STATE_INSTALLING;
    metadata->last_error = OTA_ERROR_NONE;
    if (ota_metadata_store(metadata) != 0) {
        return;
    }
    if (internal_app_valid() &&
        (((metadata->flags & OTA_META_FLAG_ACTIVE_VALID) == 0U) ||
         (package_valid(old_active, metadata, 0U, &active_header) != 0))) {
        if (backup_internal_app(old_active, metadata->active_version) != 0) {
            store_error(metadata, OTA_ERROR_STORAGE_INIT);
            return;
        }
        metadata->flags |= OTA_META_FLAG_ACTIVE_VALID;
        if (ota_metadata_store(metadata) != 0) {
            return;
        }
    }
    if (package_valid(metadata->pending_slot, metadata, 1U, &header) != 0) {
        store_error(metadata, OTA_ERROR_PAYLOAD_CRC);
        return;
    }
    metadata->previous_slot = old_active;
    if ((metadata->flags & OTA_META_FLAG_ACTIVE_VALID) != 0U) {
        metadata->flags |= OTA_META_FLAG_PREVIOUS_VALID;
    }
    /* Persist the rollback slot before erasing/programming internal flash. */
    if (ota_metadata_store(metadata) != 0) {
        return;
    }
    if (install_package(metadata->pending_slot, &header, &error) != 0) {
        if ((metadata->flags & OTA_META_FLAG_PREVIOUS_VALID) != 0U) {
            metadata->state = OTA_STATE_ROLLBACK;
            metadata->last_error = (uint16_t)error;
            (void)ota_metadata_store(metadata);
            (void)rollback(metadata);
        } else {
            store_error(metadata, error);
        }
        return;
    }
    metadata->active_slot = metadata->pending_slot;
    metadata->active_version = header.firmware_version;
    metadata->flags |= OTA_META_FLAG_ACTIVE_VALID;
    metadata->state = OTA_STATE_TESTING;
    metadata->trial_boots = 1U;
    metadata->last_error = OTA_ERROR_NONE;
    (void)ota_metadata_store(metadata);
}

void ota_boot_run(void)
{
    ota_metadata_t metadata;
    int load_result;
    if (ota_board_init() != 0) {
        jump_to_app();
        return;
    }
    load_result = ota_metadata_load(&metadata);
    if (load_result > 0) {
        (void)ota_metadata_store(&metadata);
    } else if (load_result < 0) {
        jump_to_app();
        return;
    }

    switch ((ota_state_t)metadata.state) {
    case OTA_STATE_PENDING:
        install_pending(&metadata);
        break;
    case OTA_STATE_INSTALLING:
    case OTA_STATE_ROLLBACK:
        if (internal_app_valid()) {
            store_error(&metadata, OTA_ERROR_FLASH_PROGRAM);
        } else {
            (void)rollback(&metadata);
        }
        break;
    case OTA_STATE_TESTING:
        if (!internal_app_valid() || (metadata.trial_boots >= metadata.max_trial_boots)) {
            (void)rollback(&metadata);
        } else {
            metadata.trial_boots++;
            (void)ota_metadata_store(&metadata);
        }
        break;
    case OTA_STATE_CONFIRMED:
        metadata.state = OTA_STATE_IDLE;
        metadata.trial_boots = 0U;
        metadata.last_error = OTA_ERROR_NONE;
        (void)ota_metadata_store(&metadata);
        break;
    case OTA_STATE_IDLE:
    case OTA_STATE_DOWNLOADING:
    case OTA_STATE_ERROR:
    default:
        break;
    }
    jump_to_app();
}

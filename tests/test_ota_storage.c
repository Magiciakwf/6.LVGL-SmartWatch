#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "ota_storage.h"
#include "ota_board.h"

static uint8_t eeprom[256];
static uint8_t flash[2 * 1024 * 1024];

int ota_at24_read(uint8_t address, void *data, uint16_t length)
{
    memcpy(data, &eeprom[address], length);
    return 0;
}

int ota_at24_write(uint8_t address, const void *data, uint16_t length)
{
    memcpy(&eeprom[address], data, length);
    return 0;
}

int ota_w25_read(uint32_t address, void *data, uint32_t length)
{
    memcpy(data, &flash[address], length);
    return 0;
}

int ota_board_init(void) { return 0; }
int ota_w25_init(void) { return 0; }
int ota_w25_write(uint32_t address, const void *data, uint32_t length)
{
    memcpy(&flash[address], data, length);
    return 0;
}
int ota_w25_erase_range(uint32_t address, uint32_t length)
{
    memset(&flash[address], 0xff, length);
    return 0;
}

int main(void)
{
    ota_metadata_t metadata;
    ota_metadata_t loaded;
    ota_package_header_t header;
    memset(eeprom, 0xff, sizeof(eeprom));
    memset(flash, 0xff, sizeof(flash));

    if ((offsetof(ota_package_header_t, aes_iv) != 36U) ||
        (offsetof(ota_package_header_t, key_id) != 52U) ||
        (offsetof(ota_package_header_t, header_crc32) != 56U)) return 1;

    if (ota_metadata_load(&metadata) != 1) return 2;
    if ((metadata.state != OTA_STATE_IDLE) || (metadata.max_trial_boots != 3)) return 3;
    metadata.active_version = 0x02040004;
    if (ota_metadata_store(&metadata) != 0) return 4;
    metadata.state = OTA_STATE_DOWNLOADING;
    metadata.downloaded_size = 4096;
    if (ota_metadata_store(&metadata) != 0) return 5;
    if ((ota_metadata_load(&loaded) != 0) ||
        (loaded.sequence != metadata.sequence) ||
        (loaded.downloaded_size != 4096)) return 6;

    /* Corrupt the newest even-sequence copy; the older copy must survive. */
    eeprom[OTA_META_COPY0_ADDRESS + 10] ^= 0x80;
    if ((ota_metadata_load(&loaded) != 0) ||
        (loaded.sequence + 1 != metadata.sequence) ||
        (loaded.state != OTA_STATE_IDLE)) return 7;

    memset(&header, 0, sizeof(header));
    header.magic = OTA_PACKAGE_MAGIC;
    header.format_version = OTA_PACKAGE_FORMAT_VERSION;
    header.header_size = sizeof(header);
    header.flags = OTA_PACKAGE_FLAG_AES128_CTR;
    header.firmware_version = 0x02040005;
    header.app_load_address = OTA_APP_BASE_ADDRESS;
    header.image_size = 1024;
    header.payload_size = 1024;
    header.key_id = OTA_PACKAGE_KEY_ID;
    header.header_crc32 = ota_package_header_crc(&header);
    if (!ota_package_header_valid(&header)) return 8;
    header.image_size = OTA_APP_MAX_SIZE + 1;
    if (ota_package_header_valid(&header)) return 9;
    puts("OTA storage tests passed");
    return 0;
}

#ifndef OTA_STORAGE_H
#define OTA_STORAGE_H

#include "ota_types.h"

void ota_metadata_default(ota_metadata_t *metadata);
int ota_metadata_load(ota_metadata_t *metadata);
int ota_metadata_store(ota_metadata_t *metadata);

uint32_t ota_package_header_crc(const ota_package_header_t *header);
int ota_package_header_valid(const ota_package_header_t *header);
int ota_package_read_header(uint8_t slot, ota_package_header_t *header);

#endif

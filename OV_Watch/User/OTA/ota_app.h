#ifndef OTA_APP_H
#define OTA_APP_H

#include <stdint.h>
#include "ota_types.h"

typedef struct {
    uint32_t task_id;
    uint32_t target_version;
    uint32_t package_size;
    char download_token[96];
} onenet_ota_job_t;

/* OneNET transport adapter return values. */
#define ONENET_OTA_NO_UPDATE 0
#define ONENET_OTA_UPDATE    1
#define ONENET_OTA_RETRY    -1
#define ONENET_OTA_FATAL    -2

int ota_app_init(void);
int ota_app_confirm_running_image(void);
ota_state_t ota_app_state(void);
uint8_t ota_app_progress(void);
void ota_app_task(void *argument);

/*
 * Implement these three functions in the board's IP modem/network layer.
 * Weak, safe defaults are supplied in ota_onenet_port.c.
 */
int onenet_ota_port_check(uint32_t current_version, onenet_ota_job_t *job);
int onenet_ota_port_download_range(const onenet_ota_job_t *job,
                                   uint32_t offset, uint8_t *buffer,
                                   uint32_t capacity, uint32_t *received);
void onenet_ota_port_report(const onenet_ota_job_t *job, ota_state_t state,
                            uint8_t progress, ota_error_t error);

#endif

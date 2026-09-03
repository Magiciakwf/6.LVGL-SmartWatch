#include "ota_app.h"
#include "ota_board.h"
#include "ota_storage.h"
#include "cmsis_os.h"
#include "stm32f4xx_hal.h"
#include "version.h"
#include <string.h>

#define OTA_DOWNLOAD_CHUNK       1024U
#define OTA_CHECKPOINT_INTERVAL  4096U
#define OTA_POLL_INTERVAL_MS     60000U
#define OTA_DOWNLOAD_RETRIES     5U

static ota_metadata_t g_metadata;
static uint32_t g_write_offset;
static uint32_t g_checkpoint;
static uint8_t g_initialized;

static void set_error(ota_error_t error)
{
    g_metadata.state = OTA_STATE_ERROR;
    g_metadata.last_error = (uint16_t)error;
    (void)ota_metadata_store(&g_metadata);
}

int ota_app_init(void)
{
    int load_result;
    if (g_initialized != 0U) {
        return 0;
    }
    if (ota_board_init() != 0) {
        return -1;
    }
    load_result = ota_metadata_load(&g_metadata);
    if (load_result < 0) {
        return -1;
    }
    if ((load_result > 0) && (ota_metadata_store(&g_metadata) != 0)) {
        return -1;
    }
    if ((g_metadata.state == OTA_STATE_IDLE) &&
        (g_metadata.active_version != VERSION_CODE)) {
        g_metadata.active_version = VERSION_CODE;
        if (ota_metadata_store(&g_metadata) != 0) {
            return -1;
        }
    }
    g_initialized = 1U;
    return 0;
}

int ota_app_confirm_running_image(void)
{
    onenet_ota_job_t job;
    if ((ota_app_init() != 0) || (g_metadata.state != OTA_STATE_TESTING)) {
        return 0;
    }
    g_metadata.state = OTA_STATE_CONFIRMED;
    g_metadata.trial_boots = 0U;
    g_metadata.last_error = OTA_ERROR_NONE;
    if (ota_metadata_store(&g_metadata) != 0) {
        return -1;
    }
    memset(&job, 0, sizeof(job));
    job.task_id = g_metadata.pending_task_id;
    job.target_version = g_metadata.active_version;
    job.package_size = g_metadata.pending_package_size;
    onenet_ota_port_report(&job, OTA_STATE_CONFIRMED, 100U, OTA_ERROR_NONE);
    g_metadata.state = OTA_STATE_IDLE;
    return ota_metadata_store(&g_metadata);
}

ota_state_t ota_app_state(void)
{
    return (ota_state_t)g_metadata.state;
}

uint8_t ota_app_progress(void)
{
    if (g_metadata.pending_package_size == 0U) {
        return 0U;
    }
    return (uint8_t)((g_metadata.downloaded_size * 100U) /
                     g_metadata.pending_package_size);
}

static int job_matches_resume(const onenet_ota_job_t *job)
{
    return (g_metadata.state == OTA_STATE_DOWNLOADING) &&
           (g_metadata.pending_task_id == job->task_id) &&
           (g_metadata.pending_version == job->target_version) &&
           (g_metadata.pending_package_size == job->package_size) &&
           (g_metadata.downloaded_size <= job->package_size);
}

static int prepare_job(const onenet_ota_job_t *job)
{
    uint32_t erase_size;
    if ((job->package_size <= OTA_PACKAGE_HEADER_SIZE) ||
        (job->package_size > OTA_SLOT_SIZE)) {
        set_error(OTA_ERROR_PACKAGE_SIZE);
        return -1;
    }
    /* Resume only the same OneNET task, version, and package size. */
    //判断是否需要掉电重传
    if (job_matches_resume(job)) {
        g_write_offset = g_metadata.downloaded_size;
        g_checkpoint = g_write_offset;
        return 0;
    }

    //选择新活动槽
    //写入downloading
    g_metadata.pending_slot = (uint8_t)(g_metadata.active_slot ^ 1U);
    g_metadata.pending_version = job->target_version;
    g_metadata.pending_package_size = job->package_size;
    g_metadata.pending_task_id = job->task_id;
    g_metadata.downloaded_size = 0U;
    g_metadata.last_error = OTA_ERROR_NONE;
    g_metadata.state = OTA_STATE_DOWNLOADING;
    if (ota_metadata_store(&g_metadata) != 0) {
        return -1;
    }
    erase_size = (job->package_size + 0xFFFUL) & ~0xFFFUL;//4kb对齐，计算erase的字节数(4lb的倍数)
    if (ota_w25_erase_range(ota_slot_address(g_metadata.pending_slot),
                            erase_size) != 0) {
        set_error(OTA_ERROR_STORAGE_INIT);
        return -1;
    }
    g_write_offset = 0U;
    g_checkpoint = 0U;
    return 0;
}

static int write_download_data(const uint8_t *data, uint32_t length)
{
    if ((length == 0U) ||
        ((g_write_offset + length) > g_metadata.pending_package_size) ||
        (ota_w25_write(ota_slot_address(g_metadata.pending_slot) + g_write_offset,
                       data, length) != 0)) {
        set_error(OTA_ERROR_DOWNLOAD);
        return -1;
    }
    g_write_offset += length;
    if (((g_write_offset - g_checkpoint) >= OTA_CHECKPOINT_INTERVAL) ||
        (g_write_offset == g_metadata.pending_package_size)) {
        g_metadata.downloaded_size = g_write_offset;
        if (ota_metadata_store(&g_metadata) != 0) {
            return -1;
        }
        g_checkpoint = g_write_offset;
    }
    return 0;
}

static int package_header_matches_job(const ota_package_header_t *header,
                                      const onenet_ota_job_t *job)
{
    return ota_package_header_valid(header) &&
           ((header->header_size + header->payload_size) == job->package_size) &&
           ((job->target_version == 0U) ||
            (header->firmware_version == job->target_version));
}

/* Download and validate the 256-byte package header before the payload. */
static int download_package_header(const onenet_ota_job_t *job, uint8_t *buffer)
{
    ota_package_header_t header;
    uint32_t header_offset = 0U;
    uint32_t retries = 0U;

    while (header_offset < OTA_PACKAGE_HEADER_SIZE) {
        uint32_t capacity = OTA_PACKAGE_HEADER_SIZE - header_offset;
        uint32_t received = 0U;
        int result;

        result = onenet_ota_port_download_range(job, header_offset,
                                                buffer + header_offset,
                                                capacity, &received);
        if ((result != 0) || (received == 0U) || (received > capacity)) {
            if (++retries >= OTA_DOWNLOAD_RETRIES) {
                return -1; /* Keep DOWNLOADING so the next poll can retry. */
            }
            osDelay(1000U);
            continue;
        }
        retries = 0U;
        header_offset += received;
    }

    memcpy(&header, buffer, sizeof(header));
    if (!package_header_matches_job(&header, job)) {
        set_error(OTA_ERROR_HEADER);
        return -1;
    }

    if (write_download_data(buffer, OTA_PACKAGE_HEADER_SIZE) != 0) {
        return -1;
    }

    /* Persist the validated header as the first resumable checkpoint. */
    g_metadata.downloaded_size = g_write_offset;
    if (ota_metadata_store(&g_metadata) != 0) {
        return -1;
    }
    g_checkpoint = g_write_offset;
    return 0;
}

/* A resumed download must also trust the stored header before continuing. */
static int validate_stored_package_header(const onenet_ota_job_t *job)
{
    ota_package_header_t header;

    if ((ota_package_read_header(g_metadata.pending_slot, &header) != 0) ||
        !package_header_matches_job(&header, job)) {
        set_error(OTA_ERROR_HEADER);
        return -1;
    }
    return 0;
}

/* Verify the downloaded package header before it is marked pending. */
static int finish_download(const onenet_ota_job_t *job)
{
    ota_package_header_t header;
    if (g_write_offset != g_metadata.pending_package_size) {
        set_error(OTA_ERROR_PACKAGE_SIZE);
        return -1;
    }
    if ((ota_package_read_header(g_metadata.pending_slot, &header) != 0) ||
        !package_header_matches_job(&header, job)) {
        set_error(OTA_ERROR_HEADER);
        return -1;
    }
    g_metadata.pending_version = header.firmware_version;
    g_metadata.state = OTA_STATE_PENDING;
    g_metadata.last_error = OTA_ERROR_NONE;
    if (ota_metadata_store(&g_metadata) != 0) {
        return -1;
    }
    onenet_ota_port_report(job, OTA_STATE_PENDING, 100U, OTA_ERROR_NONE);
    return 0;
}

static int download_job(const onenet_ota_job_t *job)
{
    uint8_t buffer[OTA_DOWNLOAD_CHUNK];
    uint32_t received;
    uint32_t retries = 0U;
    uint8_t last_progress = 0xFFU;
    if (prepare_job(job) != 0) {//确定写入的槽，循环从onenet下载1kb写入W25Q64
        return -1;
    }
    if (g_write_offset == 0U) {
        if (download_package_header(job, buffer) != 0) {
            return -1;
        }
    } else if (validate_stored_package_header(job) != 0) {
        return -1;
    }
    while (g_write_offset < job->package_size) {//判断
        uint32_t capacity = job->package_size - g_write_offset;
        int result;
        uint8_t progress;
        if (capacity > sizeof(buffer)) {//每次最多1024个字节
            capacity = sizeof(buffer);
        }
        received = 0U;
        result = onenet_ota_port_download_range(job, g_write_offset,
                                                buffer, capacity, &received);
        if ((result != 0) || (received == 0U) || (received > capacity)) {
            if (++retries >= OTA_DOWNLOAD_RETRIES) {
                return -1; /* Keep DOWNLOADING so the next poll resumes it. */
            }
            osDelay(1000U);
            continue;
        }
        retries = 0U;
        /* Write data and persist the resumable download offset. */
        if (write_download_data(buffer, received) != 0) {
            return -1;
        }
        progress = ota_app_progress();
        if ((progress != last_progress) && ((progress % 5U) == 0U)) {
            onenet_ota_port_report(job, OTA_STATE_DOWNLOADING,
                                   progress, OTA_ERROR_NONE);
            last_progress = progress;
        }
        osDelay(1U);
    }
    return finish_download(job);//下载结束后，置位pending
}

void ota_app_task(void *argument)
{
    onenet_ota_job_t job;
    (void)argument;
    osDelay(3000U);
    if (ota_app_init() != 0) {
        for (;;) {
            osDelay(OTA_POLL_INTERVAL_MS);
        }
    }
    if (g_metadata.state == OTA_STATE_ERROR) {
        memset(&job, 0, sizeof(job));
        job.task_id = g_metadata.pending_task_id;
        job.target_version = g_metadata.pending_version;
        job.package_size = g_metadata.pending_package_size;
        onenet_ota_port_report(&job, OTA_STATE_ERROR, ota_app_progress(),
                               (ota_error_t)g_metadata.last_error);
        g_metadata.state = OTA_STATE_IDLE;
        (void)ota_metadata_store(&g_metadata);
    }
    for (;;) {
        int result;
        memset(&job, 0, sizeof(job));
        result = onenet_ota_port_check(VERSION_CODE, &job);//检查是否有升级任务
        if (result == ONENET_OTA_UPDATE) {
            if (download_job(&job) == 0) {//如果下载，校验，状态全部成功，复位单片机
                osDelay(100U);
                NVIC_SystemReset();
            } else {
                onenet_ota_port_report(&job, (ota_state_t)g_metadata.state,
                                       ota_app_progress(),
                                       (ota_error_t)g_metadata.last_error);
            }
        }
        osDelay(OTA_POLL_INTERVAL_MS);//60s循环一次
    }
}

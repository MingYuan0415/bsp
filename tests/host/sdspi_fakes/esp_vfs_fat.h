#ifndef __SDSPI_FAKE_VFS_FAT_H__
#define __SDSPI_FAKE_VFS_FAT_H__

#include <stdbool.h>
#include <stddef.h>

#include "driver/sdspi_host.h"
#include "esp_err.h"
#include "sdmmc_cmd.h"

typedef struct esp_vfs_fat_mount_config
{
    bool format_if_mount_failed;
    int max_files;
    size_t allocation_unit_size;
    bool disk_status_check_enable;
    bool use_one_fat;
} esp_vfs_fat_mount_config_t;

esp_err_t esp_vfs_fat_sdspi_mount(
    const char *base_path, const sdmmc_host_t *host_config,
    const sdspi_device_config_t *slot_config,
    const esp_vfs_fat_mount_config_t *mount_config,
    sdmmc_card_t **out_card);
esp_err_t esp_vfs_fat_sdcard_unmount(const char *base_path,
                                     sdmmc_card_t *card);

#endif /* __SDSPI_FAKE_VFS_FAT_H__ */

#ifndef __SDSPI_FAKE_VFS_FAT_H__
#define __SDSPI_FAKE_VFS_FAT_H__
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "ff.h"
typedef struct
{
    const char *base_path;
    const char *fat_drive;
    size_t max_files;
} esp_vfs_fat_conf_t;
esp_err_t esp_vfs_fat_register(const esp_vfs_fat_conf_t *config, FATFS **fs);
esp_err_t esp_vfs_fat_unregister_path(const char *path);
esp_err_t esp_vfs_fat_info(const char *path, uint64_t *total_bytes,
                           uint64_t *free_bytes);
#endif

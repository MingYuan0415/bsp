#ifndef __BOARD_SDMMC_H__
#define __BOARD_SDMMC_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Configuration for the board's SDMMC-connected SD card. */
typedef struct board_sdmmc_config
{
    char mount_path[64];         /**< VFS mount path, normally /sdcard. */
    bool format_if_mount_failed; /**< Formatting is disabled by default. */
    int max_files;               /**< Maximum simultaneously open files. */
    size_t allocation_unit_size; /**< FAT allocation unit when formatting. */
    uint32_t max_freq_khz;       /**< Card clock; 0 selects 20 MHz. */
} board_sdmmc_config_t;

/** @brief Opaque mounted SD card instance. */
typedef struct board_sdmmc board_sdmmc_t;

/** @brief Fill a config with the P4 slot map and safe defaults. */
void board_sdmmc_config_init(board_sdmmc_config_t *config);

/**
 * @brief Mount the SD card and register its FAT filesystem with VFS.
 *
 * The card is never formatted unless format_if_mount_failed is explicitly
 * enabled. The board's SDMMC slot 0 and on-chip LDO4 are owned by the returned
 * instance until board_sdmmc_unmount().
 *
 * @param config contains the mount policy and clock.
 * @param out_device receives the owned mounted instance.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error. On failure no
 *         instance is returned and all resources are released.
 */
esp_err_t board_sdmmc_mount(const board_sdmmc_config_t *config,
                            board_sdmmc_t **out_device);

/**
 * @brief Unmount the card and release the SDMMC host and LDO.
 *
 * @param device is the instance to release; NULL is accepted.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_sdmmc_unmount(board_sdmmc_t *device);

/** @brief Report whether the card is mounted. */
bool board_sdmmc_is_mounted(const board_sdmmc_t *device);

/** @brief Return the active mount path, or an empty string. */
const char *board_sdmmc_get_mount_path(const board_sdmmc_t *device);

/**
 * @brief Erase the mounted card and mount it again.
 *
 * Destructive. Returns ESP_OK only after the reformatted filesystem is mounted
 * again and its capacity is readable.
 */
esp_err_t board_sdmmc_format(board_sdmmc_t *device);

/**
 * @brief Read the mounted filesystem's total and free capacity.
 *
 * @param device is the mounted instance.
 * @param total_bytes receives total capacity in bytes.
 * @param free_bytes receives free capacity in bytes.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_sdmmc_get_space(board_sdmmc_t *device, uint64_t *total_bytes,
                                uint64_t *free_bytes);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_SDMMC_H__ */

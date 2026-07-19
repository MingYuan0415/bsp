#ifndef __BOARD_SDSPI_H__
#define __BOARD_SDSPI_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "sdmmc_cmd.h"

#include "board_tca9554.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Configuration for the board's SPI-connected SD card. */
typedef struct board_sdspi_config
{
    board_tca9554_t *io_expander; /**< TCA9554 driving the SD CS line. */
    spi_host_device_t host;       /**< SPI host used by the SD card. */
    gpio_num_t mosi_io_num;      /**< SD MOSI GPIO. */
    gpio_num_t miso_io_num;      /**< SD MISO GPIO. */
    gpio_num_t sclk_io_num;      /**< SD clock GPIO. */
    uint8_t cs_pin_mask;         /**< TCA pin mask for SD CS (active low). */
    uint32_t max_freq_khz;       /**< Card clock, normally 20 MHz. */
    char mount_path[64];         /**< VFS mount path, normally /sdcard. */
    bool format_if_mount_failed; /**< Formatting is disabled by default. */
    int max_files;               /**< Maximum simultaneously open files. */
    size_t allocation_unit_size; /**< FAT allocation unit when formatting. */
} board_sdspi_config_t;

/** @brief Opaque mounted SD card instance. */
typedef struct board_sdspi board_sdspi_t;

/** @brief Fill a config with the schematic pin map and safe defaults. */
void board_sdspi_config_init(board_sdspi_config_t *config);

/**
 * @brief Mount the SD card and register its FAT filesystem with VFS.
 *
 * @param config contains the SPI and TCA9554 configuration.
 * @param out_device receives the owned mounted instance.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error. The card is never
 *         formatted unless format_if_mount_failed is explicitly enabled. If
 *         rollback fails, out_device receives a partial instance which must
 *         be passed to board_sdspi_unmount() until cleanup succeeds.
 */
esp_err_t board_sdspi_mount(const board_sdspi_config_t *config,
                            board_sdspi_t **out_device);

/**
 * @brief Unmount the card and release the SPI bus and TCA CS ownership.
 *
 * This also retries cleanup for a partial instance returned by a failed mount.
 */
esp_err_t board_sdspi_unmount(board_sdspi_t *device);

/** @brief Return whether the instance currently owns a mounted card. */
bool board_sdspi_is_mounted(const board_sdspi_t *device);

/** @brief Return the card descriptor owned by a mounted instance. */
sdmmc_card_t *board_sdspi_get_card(board_sdspi_t *device);

/** @brief Return the configured VFS path, or NULL for an invalid instance. */
const char *board_sdspi_get_mount_path(const board_sdspi_t *device);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_SDSPI_H__ */

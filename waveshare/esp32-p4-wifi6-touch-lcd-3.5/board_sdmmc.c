/**
 * @brief  ESP32-P4 board SD card on SDMMC slot 0
 * @note   The card uses the slot-0 IOMUX pins (CLK 43, CMD 44, D0-D3 39-42)
 *         and the on-chip LDO4 which supplies the SDMMC IO rail. The C6 hosted
 *         SDIO lives on slot 1, so the two devices do not share pins. No card
 *         detect pin exists, so mounting is explicit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

#include "board_sdmmc.h"

#define BOARD_SDMMC_DEFAULT_MOUNT      "/sdcard"
#define BOARD_SDMMC_DEFAULT_MAX_FILES  (5)
#define BOARD_SDMMC_DEFAULT_ALLOC_UNIT (16U * 1024U)
#define BOARD_SDMMC_DEFAULT_FREQ_KHZ   (20000U)
#define BOARD_SDMMC_SLOT               (0)
#define BOARD_SDMMC_LDO_CHAN           (4)
#define BOARD_SDMMC_WIDTH              (4)

struct board_sdmmc
{
    sdmmc_host_t host;
    sdmmc_card_t *card;
    sd_pwr_ctrl_handle_t pwr_ctrl;
    char mount_path[64];
    bool mounted;
};

static board_sdmmc_t *s_active;

/*
 * P4 rev v1.3 can lose the card clock between commands when the SDHOST
 * low-power gate is enabled. Keep CCLK on for the whole mounted lifetime;
 * this also keeps the ACMD41 window stable on cards that are slow to enter
 * the idle state.
 */
static esp_err_t _board_sdmmc_keep_clock_on(int slot, bool always_on)
{
    (void)always_on;
    return sdmmc_host_set_cclk_always_on(slot, true);
}

void board_sdmmc_config_init(board_sdmmc_config_t *config)
{
    if (config == NULL)
    {
        return;
    }
    *config = (board_sdmmc_config_t)
    {
        .mount_path = BOARD_SDMMC_DEFAULT_MOUNT,
        .format_if_mount_failed = false,
        .max_files = BOARD_SDMMC_DEFAULT_MAX_FILES,
        .allocation_unit_size = BOARD_SDMMC_DEFAULT_ALLOC_UNIT,
        .max_freq_khz = BOARD_SDMMC_DEFAULT_FREQ_KHZ,
    };
}

static void _board_sdmmc_release(board_sdmmc_t *device)
{
    if (device == NULL)
    {
        return;
    }
    if (device->card != NULL)
    {
        (void)esp_vfs_fat_sdcard_unmount(device->mount_path, device->card);
        device->card = NULL;
    }
    if (device->pwr_ctrl != NULL)
    {
        (void)sd_pwr_ctrl_del_on_chip_ldo(device->pwr_ctrl);
        device->pwr_ctrl = NULL;
    }
    free(device);
}

esp_err_t board_sdmmc_mount(const board_sdmmc_config_t *config,
                            board_sdmmc_t **out_device)
{
    if (config == NULL || out_device == NULL || config->mount_path[0] == '\0')
    {
        return ESP_ERR_INVALID_ARG;
    }
    *out_device = NULL;
    if (s_active != NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    board_sdmmc_t *device = calloc(1U, sizeof(*device));
    if (device == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    (void)snprintf(device->mount_path, sizeof(device->mount_path), "%s",
                   config->mount_path);

    device->host = (sdmmc_host_t)SDMMC_HOST_DEFAULT();
    device->host.slot = BOARD_SDMMC_SLOT;
    device->host.set_cclk_always_on = _board_sdmmc_keep_clock_on;
    device->host.max_freq_khz = config->max_freq_khz != 0U ?
                                config->max_freq_khz :
                                BOARD_SDMMC_DEFAULT_FREQ_KHZ;

    const sd_pwr_ctrl_ldo_config_t ldo_config =
    {
        .ldo_chan_id = BOARD_SDMMC_LDO_CHAN,
    };
    esp_err_t result = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config,
        &device->pwr_ctrl);
    if (result != ESP_OK)
    {
        free(device);
        return result;
    }
    device->host.pwr_ctrl_handle = device->pwr_ctrl;

    /* Slot 0 is wired to the dedicated IOMUX pins; leaving the pin fields
     * unset selects those fixed pins instead of the GPIO matrix. */
    const sdmmc_slot_config_t slot_config =
    {
        .cd = SDMMC_SLOT_NO_CD,
        .wp = SDMMC_SLOT_NO_WP,
        .width = BOARD_SDMMC_WIDTH,
        .flags = 0,
    };

    const esp_vfs_fat_mount_config_t mount_config =
    {
        .format_if_mount_failed = config->format_if_mount_failed,
        .max_files = config->max_files > 0 ? config->max_files :
        BOARD_SDMMC_DEFAULT_MAX_FILES,
        .allocation_unit_size = config->allocation_unit_size,
    };

    result = esp_vfs_fat_sdmmc_mount(device->mount_path, &device->host,
                                     &slot_config, &mount_config,
                                     &device->card);
    if (result != ESP_OK)
    {
        _board_sdmmc_release(device);
        return result;
    }

    device->mounted = true;
    s_active = device;
    *out_device = device;
    return ESP_OK;
}

esp_err_t board_sdmmc_unmount(board_sdmmc_t *device)
{
    if (device == NULL)
    {
        return ESP_OK;
    }
    if (device != s_active && s_active != NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    s_active = NULL;
    device->mounted = false;
    _board_sdmmc_release(device);
    return ESP_OK;
}

bool board_sdmmc_is_mounted(const board_sdmmc_t *device)
{
    return device != NULL && device->mounted && device->card != NULL;
}

const char *board_sdmmc_get_mount_path(const board_sdmmc_t *device)
{
    return device != NULL ? device->mount_path : "";
}

esp_err_t board_sdmmc_format(board_sdmmc_t *device)
{
    if (!board_sdmmc_is_mounted(device))
    {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_vfs_fat_sdcard_format(device->mount_path, device->card);
}

esp_err_t board_sdmmc_get_space(board_sdmmc_t *device, uint64_t *total_bytes,
                                uint64_t *free_bytes)
{
    if (!board_sdmmc_is_mounted(device) || total_bytes == NULL ||
            free_bytes == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_vfs_fat_info(device->mount_path, total_bytes, free_bytes);
}

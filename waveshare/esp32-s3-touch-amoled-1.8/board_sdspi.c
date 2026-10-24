#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "esp_io_expander_gpio_wrapper.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "soc/gpio_num.h"

#include "board_sdspi.h"

#define BOARD_SDSPI_DEFAULT_HOST       (SPI3_HOST)
#define BOARD_SDSPI_DEFAULT_MOSI       (GPIO_NUM_1)
#define BOARD_SDSPI_DEFAULT_MISO       (GPIO_NUM_3)
#define BOARD_SDSPI_DEFAULT_SCLK       (GPIO_NUM_2)
#define BOARD_SDSPI_DEFAULT_CS_MASK    (1U << 7)
#define BOARD_SDSPI_DEFAULT_FREQ_KHZ   (20000U)
#define BOARD_SDSPI_DEFAULT_MAX_FILES  (5)
#define BOARD_SDSPI_DEFAULT_ALLOC_UNIT (16U * 1024U)
#define BOARD_SDSPI_MAX_TRANSFER       (4096U)
#define BOARD_SDSPI_FORMAT_WORK_BYTES  (4096U)
#define BOARD_SDSPI_VIRTUAL_GPIO_BASE  ((uint32_t)GPIO_NUM_MAX)

struct board_sdspi
{
    board_sdspi_config_t config;
    esp_io_expander_handle_t io_expander;
    sdmmc_card_t card;
    sdspi_dev_handle_t sdspi_handle;
    FATFS *filesystem;
    BYTE drive;
    char drive_path[3];
    bool gpio_wrapper_registered;
    bool spi_bus_initialized;
    bool sdspi_initialized;
    bool disk_registered;
    bool vfs_registered;
    bool vfs_route_removed;
    bool fat_attached;
    bool mounted;
};

static board_sdspi_t *s_active_device;

static int _board_sdspi_pin_index(uint8_t pin_mask)
{
    if (pin_mask == 0U || (pin_mask & (uint8_t)(pin_mask - 1U)) != 0U)
    {
        return -1;
    }
    return __builtin_ctz((unsigned)pin_mask);
}

static bool _board_sdspi_allocation_unit_valid(size_t size)
{
    return size == 0U || (size >= 512U && size <= 65536U &&
                          (size & (size - 1U)) == 0U);
}

static esp_err_t _board_sdspi_register_virtual_gpio(board_sdspi_t *device)
{
#if CONFIG_IO_EXPANDER_ENABLE_GPIO_API_WRAPPER
    const esp_err_t result = esp_io_expander_gpio_wrapper_append_handler(
                                 device->io_expander, BOARD_SDSPI_VIRTUAL_GPIO_BASE);
    if (result == ESP_OK)
    {
        device->gpio_wrapper_registered = true;
    }
    return result;
#else
    (void)device;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t _board_sdspi_detach_fat(board_sdspi_t *device)
{
    device->mounted = false;
    if (device->fat_attached)
    {
        if (f_mount(NULL, device->drive_path, 0U) != FR_OK)
        {
            return ESP_FAIL;
        }
        device->fat_attached = false;
    }
    return ESP_OK;
}

static esp_err_t _board_sdspi_release_partial(board_sdspi_t *device)
{
    esp_err_t result = _board_sdspi_detach_fat(device);
    if (result != ESP_OK)
    {
        return result;
    }
    if (device->vfs_registered)
    {
        if (!device->vfs_route_removed)
        {
            result = esp_vfs_unregister(device->config.mount_path);
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
            {
                return result;
            }
            device->vfs_route_removed = true;
        }
        /* IDF destroys this context even on error. Remove its raw route
         * first so no registered VFS callback can retain a freed context.
         * The helper's second raw removal now returns INVALID_STATE. */
        result = esp_vfs_fat_unregister_path(device->config.mount_path);
        device->vfs_registered = false;
        device->filesystem = NULL;
        if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
        {
            return result;
        }
    }
    if (device->disk_registered)
    {
        ff_diskio_unregister(device->drive);
        device->disk_registered = false;
    }
    if (device->sdspi_initialized)
    {
        result = sdspi_host_remove_device(device->sdspi_handle);
        if (result != ESP_OK)
        {
            return result;
        }
        device->sdspi_initialized = false;
    }
    if (device->spi_bus_initialized)
    {
        result = spi_bus_free(device->config.host);
        if (result != ESP_OK)
        {
            return result;
        }
        device->spi_bus_initialized = false;
    }
    if (device->gpio_wrapper_registered)
    {
        result = esp_io_expander_gpio_wrapper_remove_handler(device->io_expander);
        if (result != ESP_OK)
        {
            return result;
        }
        device->gpio_wrapper_registered = false;
    }
    s_active_device = NULL;
    free(device);
    return ESP_OK;
}

static FRESULT _board_sdspi_mount_fat(board_sdspi_t *device)
{
    /* f_mount registers the pointer before probing, including on failure. */
    device->fat_attached = true;
    return f_mount(device->filesystem, device->drive_path, 1U);
}

static esp_err_t _board_sdspi_create_volume(board_sdspi_t *device, bool partition)
{
    void *work = malloc(BOARD_SDSPI_FORMAT_WORK_BYTES);
    if (work == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    FRESULT result = FR_OK;
    if (partition)
    {
        const LBA_t partitions[4] = {100U, 0U, 0U, 0U};
        result = f_fdisk(device->drive, partitions, work);
    }
    if (result == FR_OK)
    {
        size_t allocation_unit = device->config.allocation_unit_size;
        const size_t sector_size = device->card.csd.sector_size;
        if (allocation_unit < sector_size)
        {
            allocation_unit = sector_size;
        }
        if (allocation_unit > sector_size * 128U)
        {
            allocation_unit = sector_size * 128U;
        }
        const MKFS_PARM options =
        {
            .fmt = FM_ANY,
            .n_fat = 2U,
            .align = 0U,
            .n_root = 0U,
            .au_size = allocation_unit,
        };
        result = f_mkfs(device->drive_path, &options, work,
                        BOARD_SDSPI_FORMAT_WORK_BYTES);
    }
    free(work);
    return result == FR_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t _board_sdspi_verify_capacity(board_sdspi_t *device)
{
    uint64_t total;
    uint64_t available;
    const esp_err_t result = esp_vfs_fat_info(device->config.mount_path,
        &total, &available);
    if (result != ESP_OK)
    {
        return result;
    }
    return total != 0U ? ESP_OK : ESP_FAIL;
}

void board_sdspi_config_init(board_sdspi_config_t *config)
{
    if (config == NULL)
    {
        return;
    }
    *config = (board_sdspi_config_t)
    {
        .io_expander = NULL,
        .host = BOARD_SDSPI_DEFAULT_HOST,
        .mosi_io_num = BOARD_SDSPI_DEFAULT_MOSI,
        .miso_io_num = BOARD_SDSPI_DEFAULT_MISO,
        .sclk_io_num = BOARD_SDSPI_DEFAULT_SCLK,
        .cs_pin_mask = BOARD_SDSPI_DEFAULT_CS_MASK,
        .max_freq_khz = BOARD_SDSPI_DEFAULT_FREQ_KHZ,
        .mount_path = "/sdcard",
        .format_if_mount_failed = false,
        .max_files = BOARD_SDSPI_DEFAULT_MAX_FILES,
        .allocation_unit_size = BOARD_SDSPI_DEFAULT_ALLOC_UNIT,
    };
}

esp_err_t board_sdspi_mount(const board_sdspi_config_t *config,
                            board_sdspi_t **out_device)
{
    if (config == NULL || out_device == NULL || config->io_expander == NULL ||
            _board_sdspi_pin_index(config->cs_pin_mask) < 0 ||
            config->mount_path[0] != '/' ||
            memchr(config->mount_path, '\0', sizeof(config->mount_path)) == NULL ||
            config->max_freq_khz < 400U || config->max_freq_khz > 40000U ||
            config->max_files <= 0 ||
            !_board_sdspi_allocation_unit_valid(config->allocation_unit_size))
    {
        return ESP_ERR_INVALID_ARG;
    }
    *out_device = NULL;
    if (s_active_device != NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    board_sdspi_t *device = calloc(1U, sizeof(*device));
    if (device == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    device->config = *config;
    device->io_expander = board_tca9554_get_expander(config->io_expander);
    if (device->io_expander == NULL)
    {
        free(device);
        return ESP_ERR_INVALID_STATE;
    }
    s_active_device = device;
    esp_err_t result = board_tca9554_set_direction(config->io_expander,
        config->cs_pin_mask, true);
    if (result == ESP_OK)
    {
        result = board_tca9554_set_output_level(config->io_expander,
                                                config->cs_pin_mask, 1U);
    }
    if (result == ESP_OK)
    {
        result = _board_sdspi_register_virtual_gpio(device);
    }
    if (result != ESP_OK)
    {
        goto cleanup;
    }
    const spi_bus_config_t bus_config =
    {
        .mosi_io_num = config->mosi_io_num,
        .miso_io_num = config->miso_io_num,
        .sclk_io_num = config->sclk_io_num,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = BOARD_SDSPI_MAX_TRANSFER,
    };
    result = spi_bus_initialize(config->host, &bus_config, SDSPI_DEFAULT_DMA);
    if (result != ESP_OK)
    {
        goto cleanup;
    }
    device->spi_bus_initialized = true;
    result = sdspi_host_init();
    if (result != ESP_OK)
    {
        goto cleanup;
    }
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = config->host;
    slot_config.gpio_cs = (gpio_num_t)(BOARD_SDSPI_VIRTUAL_GPIO_BASE +
                                       (uint32_t)_board_sdspi_pin_index(config->cs_pin_mask));
    slot_config.gpio_cd = SDSPI_SLOT_NO_CD;
    slot_config.gpio_wp = SDSPI_SLOT_NO_WP;
    slot_config.gpio_int = SDSPI_SLOT_NO_INT;
    result = sdspi_host_init_device(&slot_config, &device->sdspi_handle);
    if (result != ESP_OK)
    {
        goto cleanup;
    }
    device->sdspi_initialized = true;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = device->sdspi_handle;
    host.max_freq_khz = (int)config->max_freq_khz;
    result = sdmmc_card_init(&host, &device->card);
    if (result != ESP_OK)
    {
        goto cleanup;
    }
    result = ff_diskio_get_drive(&device->drive);
    if (result != ESP_OK)
    {
        goto cleanup;
    }
    ff_diskio_register_sdmmc(device->drive, &device->card);
    device->disk_registered = true;
    ff_sdmmc_set_disk_status_check(device->drive, false);
    device->drive_path[0] = (char)('0' + device->drive);
    device->drive_path[1] = ':';
    const esp_vfs_fat_conf_t vfs_config =
    {
        .base_path = device->config.mount_path,
        .fat_drive = device->drive_path,
        .max_files = config->max_files,
    };
    result = esp_vfs_fat_register(&vfs_config, &device->filesystem);
    if (result != ESP_OK)
    {
        /* An existing registration is not ours, even if IDF returns its fs. */
        device->filesystem = NULL;
        goto cleanup;
    }
    device->vfs_registered = true;
    const FRESULT mount_result = _board_sdspi_mount_fat(device);
    if (mount_result != FR_OK)
    {
        result = ESP_FAIL;
        if (!config->format_if_mount_failed ||
                (mount_result != FR_NO_FILESYSTEM && mount_result != FR_INT_ERR))
        {
            goto cleanup;
        }
        result = _board_sdspi_detach_fat(device);
        if (result != ESP_OK)
        {
            goto cleanup;
        }
        result = _board_sdspi_create_volume(device, true);
        if (result != ESP_OK)
        {
            goto cleanup;
        }
        if (_board_sdspi_mount_fat(device) != FR_OK)
        {
            result = ESP_FAIL;
            goto cleanup;
        }
        result = _board_sdspi_verify_capacity(device);
        if (result != ESP_OK)
        {
            goto cleanup;
        }
    }
    device->mounted = true;
    *out_device = device;
    return ESP_OK;

cleanup:
    {
        const esp_err_t cleanup_result = _board_sdspi_release_partial(device);
        if (cleanup_result != ESP_OK)
        {
            *out_device = device;
            return cleanup_result;
        }
    }
    return result;
}

esp_err_t board_sdspi_unmount(board_sdspi_t *device)
{
    if (device == NULL || s_active_device != device)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return _board_sdspi_release_partial(device);
}

bool board_sdspi_is_mounted(const board_sdspi_t *device)
{
    return device != NULL && s_active_device == device && device->mounted;
}

const char *board_sdspi_get_mount_path(const board_sdspi_t *device)
{
    return device == NULL ? NULL : device->config.mount_path;
}

esp_err_t board_sdspi_get_space(const board_sdspi_t *device,
                                uint64_t *total_bytes, uint64_t *free_bytes)
{
    if (device == NULL || total_bytes == NULL || free_bytes == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!board_sdspi_is_mounted(device))
    {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t total;
    uint64_t available;
    const esp_err_t result = esp_vfs_fat_info(device->config.mount_path,
        &total, &available);
    if (result == ESP_OK)
    {
        *total_bytes = total;
        *free_bytes = available;
    }
    return result;
}

esp_err_t board_sdspi_format(board_sdspi_t *device)
{
    if (!board_sdspi_is_mounted(device))
    {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = _board_sdspi_detach_fat(device);
    if (result != ESP_OK)
    {
        return result;
    }
    result = _board_sdspi_create_volume(device, false);
    if (result != ESP_OK)
    {
        return result;
    }
    if (_board_sdspi_mount_fat(device) != FR_OK)
    {
        return ESP_FAIL;
    }
    result = _board_sdspi_verify_capacity(device);
    if (result == ESP_OK)
    {
        device->mounted = true;
    }
    return result;
}

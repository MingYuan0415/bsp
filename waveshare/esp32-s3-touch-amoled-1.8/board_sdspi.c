#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_io_expander_gpio_wrapper.h"
#include "esp_vfs_fat.h"
#include "soc/gpio_num.h"

#include "board_sdspi.h"

#define BOARD_SDSPI_DEFAULT_HOST       (SPI3_HOST)
#define BOARD_SDSPI_DEFAULT_MOSI       (GPIO_NUM_1)
#define BOARD_SDSPI_DEFAULT_MISO       (GPIO_NUM_3)
#define BOARD_SDSPI_DEFAULT_SCLK       (GPIO_NUM_2)
#define BOARD_SDSPI_DEFAULT_CS_MASK    (1U << 7)
#define BOARD_SDSPI_DEFAULT_FREQ_KHZ   (20000U)
#define BOARD_SDSPI_DEFAULT_MAX_FILES   (5)
#define BOARD_SDSPI_DEFAULT_ALLOC_UNIT (16U * 1024U)
#define BOARD_SDSPI_MAX_TRANSFER       (4096U)
#define BOARD_SDSPI_VIRTUAL_GPIO_BASE  ((uint32_t)GPIO_NUM_MAX)

struct board_sdspi
{
    board_sdspi_config_t config;
    esp_io_expander_handle_t io_expander;
    sdmmc_card_t *card;
    bool gpio_wrapper_registered;
    bool spi_bus_initialized;
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
    esp_err_t result = esp_io_expander_gpio_wrapper_append_handler(
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

static esp_err_t _board_sdspi_release_partial(board_sdspi_t *device)
{
    esp_err_t first_error = ESP_OK;
    if (device == NULL)
    {
        return first_error;
    }

    if (device->spi_bus_initialized)
    {
        esp_err_t result = spi_bus_free(device->config.host);
        if (result == ESP_OK)
        {
            device->spi_bus_initialized = false;
        }
        else
        {
            first_error = result;
        }
    }
    if (!device->spi_bus_initialized && device->gpio_wrapper_registered)
    {
        esp_err_t result = esp_io_expander_gpio_wrapper_remove_handler(
                               device->io_expander);
        if (result == ESP_OK)
        {
            device->gpio_wrapper_registered = false;
        }
        else if (first_error == ESP_OK)
        {
            first_error = result;
        }
    }
    if (!device->spi_bus_initialized && !device->gpio_wrapper_registered &&
            !device->mounted && device->card == NULL)
    {
        if (s_active_device == device)
        {
            s_active_device = NULL;
        }
        free(device);
    }
    return first_error;
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
            strlen(config->mount_path) >= sizeof(((board_sdspi_config_t *)0)->mount_path) ||
            config->max_freq_khz < 400U || config->max_freq_khz > 40000U ||
            config->max_files <= 0 ||
            !_board_sdspi_allocation_unit_valid(config->allocation_unit_size))
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_active_device != NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    *out_device = NULL;

    board_sdspi_t *device = calloc(1, sizeof(*device));
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

    esp_err_t result = board_tca9554_set_direction(
                           config->io_expander, config->cs_pin_mask, true);
    if (result == ESP_OK)
    {
        result = board_tca9554_set_output_level(
                     config->io_expander, config->cs_pin_mask, 1U);
    }
    if (result == ESP_OK)
    {
        result = _board_sdspi_register_virtual_gpio(device);
    }
    if (result != ESP_OK)
    {
        device->card = NULL;
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

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = config->host;
    host.max_freq_khz = (int)config->max_freq_khz;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = config->host;
    slot_config.gpio_cs = (gpio_num_t)(
                              BOARD_SDSPI_VIRTUAL_GPIO_BASE +
                              (uint32_t)_board_sdspi_pin_index(config->cs_pin_mask));
    slot_config.gpio_cd = SDSPI_SLOT_NO_CD;
    slot_config.gpio_wp = SDSPI_SLOT_NO_WP;
    slot_config.gpio_int = SDSPI_SLOT_NO_INT;

    const esp_vfs_fat_mount_config_t mount_config =
    {
        .format_if_mount_failed = config->format_if_mount_failed,
        .max_files = config->max_files,
        .allocation_unit_size = config->allocation_unit_size,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };
    result = esp_vfs_fat_sdspi_mount(device->config.mount_path,
                                     &host,
                                     &slot_config,
                                     &mount_config,
                                     &device->card);
    if (result != ESP_OK)
    {
        goto cleanup;
    }

    device->mounted = true;
    *out_device = device;
    return ESP_OK;

cleanup:
    {
        esp_err_t cleanup_result = _board_sdspi_release_partial(device);
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

    esp_err_t result = ESP_OK;
    if (device->mounted)
    {
        if (device->card == NULL)
        {
            return ESP_ERR_INVALID_STATE;
        }
        result = esp_vfs_fat_sdcard_unmount(device->config.mount_path,
                                            device->card);
        if (result != ESP_OK)
        {
            return result;
        }
        device->card = NULL;
        device->mounted = false;
    }
    else if (device->card != NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    result = _board_sdspi_release_partial(device);
    return result;
}

bool board_sdspi_is_mounted(const board_sdspi_t *device)
{
    return device != NULL && device->mounted;
}

sdmmc_card_t *board_sdspi_get_card(board_sdspi_t *device)
{
    return device == NULL ? NULL : device->card;
}

const char *board_sdspi_get_mount_path(const board_sdspi_t *device)
{
    return device == NULL ? NULL : device->config.mount_path;
}

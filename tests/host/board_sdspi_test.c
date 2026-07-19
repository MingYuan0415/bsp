#include "board_sdspi.h"

#include "driver/sdspi_host.h"
#include "esp_io_expander_gpio_wrapper.h"
#include "esp_vfs_fat.h"
#include "soc/gpio_num.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST_CS_MASK (UINT8_C(1) << 7)
#define TEST_CLEANUP_ERROR 0x5501

struct board_tca9554
{
    int unused;
};

typedef struct test_state
{
    unsigned direction_calls;
    unsigned level_calls;
    unsigned wrapper_append_calls;
    unsigned wrapper_remove_calls;
    unsigned spi_init_calls;
    unsigned spi_free_calls;
    unsigned mount_calls;
    unsigned unmount_calls;
    uint8_t direction_mask;
    bool direction_output;
    uint8_t level_mask;
    uint8_t level;
    esp_io_expander_handle_t wrapper_handle;
    uint32_t virtual_gpio_base;
    spi_host_device_t spi_host;
    spi_bus_config_t bus_config;
    spi_dma_chan_t dma_channel;
    char mount_path[64];
    sdmmc_host_t host_config;
    sdspi_device_config_t slot_config;
    esp_vfs_fat_mount_config_t mount_config;
    esp_err_t mount_result;
    esp_err_t unmount_result;
    unsigned spi_free_failures;
    unsigned wrapper_remove_failures;
} test_state_t;

static test_state_t s_test;
static struct board_tca9554 s_tca;
static int s_expander;
static sdmmc_card_t s_card;

static void _test_reset(void)
{
    memset(&s_test, 0, sizeof(s_test));
    s_test.mount_result = ESP_OK;
    s_test.unmount_result = ESP_OK;
}

esp_io_expander_handle_t board_tca9554_get_expander(
    board_tca9554_t *device)
{
    return device == &s_tca ? &s_expander : NULL;
}

esp_err_t board_tca9554_set_direction(board_tca9554_t *device,
                                      uint8_t pin_mask, bool output)
{
    assert(device == &s_tca);
    ++s_test.direction_calls;
    s_test.direction_mask = pin_mask;
    s_test.direction_output = output;
    return ESP_OK;
}

esp_err_t board_tca9554_set_output_level(board_tca9554_t *device,
        uint8_t pin_mask, uint8_t level)
{
    assert(device == &s_tca);
    ++s_test.level_calls;
    s_test.level_mask = pin_mask;
    s_test.level = level;
    return ESP_OK;
}

esp_err_t esp_io_expander_gpio_wrapper_append_handler(
    esp_io_expander_handle_t handler, uint32_t start_io_num)
{
    ++s_test.wrapper_append_calls;
    s_test.wrapper_handle = handler;
    s_test.virtual_gpio_base = start_io_num;
    return ESP_OK;
}

esp_err_t esp_io_expander_gpio_wrapper_remove_handler(
    esp_io_expander_handle_t handler)
{
    assert(handler == &s_expander);
    ++s_test.wrapper_remove_calls;
    if (s_test.wrapper_remove_failures > 0U)
    {
        --s_test.wrapper_remove_failures;
        return TEST_CLEANUP_ERROR;
    }
    return ESP_OK;
}

esp_err_t spi_bus_initialize(spi_host_device_t host,
                             const spi_bus_config_t *bus_config,
                             spi_dma_chan_t dma_channel)
{
    ++s_test.spi_init_calls;
    s_test.spi_host = host;
    s_test.bus_config = *bus_config;
    s_test.dma_channel = dma_channel;
    return ESP_OK;
}

esp_err_t spi_bus_free(spi_host_device_t host)
{
    assert(host == SPI3_HOST);
    ++s_test.spi_free_calls;
    if (s_test.spi_free_failures > 0U)
    {
        --s_test.spi_free_failures;
        return TEST_CLEANUP_ERROR;
    }
    return ESP_OK;
}

esp_err_t esp_vfs_fat_sdspi_mount(
    const char *base_path, const sdmmc_host_t *host_config,
    const sdspi_device_config_t *slot_config,
    const esp_vfs_fat_mount_config_t *mount_config,
    sdmmc_card_t **out_card)
{
    ++s_test.mount_calls;
    memcpy(s_test.mount_path, base_path, strlen(base_path) + 1U);
    s_test.host_config = *host_config;
    s_test.slot_config = *slot_config;
    s_test.mount_config = *mount_config;
    if (s_test.mount_result == ESP_OK)
    {
        *out_card = &s_card;
    }
    return s_test.mount_result;
}

esp_err_t esp_vfs_fat_sdcard_unmount(const char *base_path,
                                     sdmmc_card_t *card)
{
    assert(strcmp(base_path, "/sdcard") == 0);
    assert(card == &s_card);
    ++s_test.unmount_calls;
    return s_test.unmount_result;
}

static board_sdspi_t *_test_mount_default(void)
{
    board_sdspi_config_t config;
    board_sdspi_config_init(&config);
    config.io_expander = &s_tca;

    board_sdspi_t *device = NULL;
    assert(board_sdspi_mount(&config, &device) == ESP_OK);
    assert(device != NULL);
    return device;
}

static void _test_success_and_virtual_cs(void)
{
    _test_reset();
    board_sdspi_config_t config;
    board_sdspi_config_init(&config);
    assert(config.host == SPI3_HOST);
    assert(config.mosi_io_num == GPIO_NUM_1);
    assert(config.miso_io_num == GPIO_NUM_3);
    assert(config.sclk_io_num == GPIO_NUM_2);
    assert(config.cs_pin_mask == TEST_CS_MASK);
    assert(config.max_freq_khz == 20000U);
    assert(strcmp(config.mount_path, "/sdcard") == 0);
    assert(!config.format_if_mount_failed);
    config.io_expander = &s_tca;

    board_sdspi_t *device = NULL;
    assert(board_sdspi_mount(&config, &device) == ESP_OK);
    assert(device != NULL);
    assert(board_sdspi_is_mounted(device));
    assert(board_sdspi_get_card(device) == &s_card);
    assert(strcmp(board_sdspi_get_mount_path(device), "/sdcard") == 0);
    assert(s_test.direction_calls == 1U);
    assert(s_test.direction_mask == TEST_CS_MASK);
    assert(s_test.direction_output);
    assert(s_test.level_calls == 1U);
    assert(s_test.level_mask == TEST_CS_MASK);
    assert(s_test.level == 1U);
    assert(s_test.wrapper_append_calls == 1U);
    assert(s_test.wrapper_handle == &s_expander);
    assert(s_test.virtual_gpio_base == (uint32_t)GPIO_NUM_MAX);
    assert(s_test.spi_init_calls == 1U);
    assert(s_test.spi_host == SPI3_HOST);
    assert(s_test.bus_config.mosi_io_num == GPIO_NUM_1);
    assert(s_test.bus_config.miso_io_num == GPIO_NUM_3);
    assert(s_test.bus_config.sclk_io_num == GPIO_NUM_2);
    assert(s_test.bus_config.max_transfer_sz == 4096);
    assert(s_test.dma_channel == SDSPI_DEFAULT_DMA);
    assert(s_test.mount_calls == 1U);
    assert(s_test.host_config.slot == SPI3_HOST);
    assert(s_test.host_config.max_freq_khz == 20000);
    assert(s_test.slot_config.host_id == SPI3_HOST);
    assert(s_test.slot_config.gpio_cs == GPIO_NUM_MAX + 7);
    assert(s_test.slot_config.gpio_cd == SDSPI_SLOT_NO_CD);
    assert(s_test.slot_config.gpio_wp == SDSPI_SLOT_NO_WP);
    assert(s_test.slot_config.gpio_int == SDSPI_SLOT_NO_INT);
    assert(!s_test.mount_config.format_if_mount_failed);
    assert(s_test.mount_config.max_files == 5);
    assert(s_test.mount_config.allocation_unit_size == 16U * 1024U);

    assert(board_sdspi_unmount(device) == ESP_OK);
    assert(s_test.unmount_calls == 1U);
    assert(s_test.spi_free_calls == 1U);
    assert(s_test.wrapper_remove_calls == 1U);
}

static void _test_invalid_filesystem_config(void)
{
    _test_reset();
    board_sdspi_config_t config;
    board_sdspi_config_init(&config);
    config.io_expander = &s_tca;
    board_sdspi_t *device = NULL;

    memcpy(config.mount_path, "relative", sizeof("relative"));
    assert(board_sdspi_mount(&config, &device) == ESP_ERR_INVALID_ARG);
    assert(device == NULL);

    board_sdspi_config_init(&config);
    config.io_expander = &s_tca;
    config.allocation_unit_size = 1000U;
    assert(board_sdspi_mount(&config, &device) == ESP_ERR_INVALID_ARG);
    assert(device == NULL);
}

static void _test_mount_rollback_retry(void)
{
    _test_reset();
    s_test.mount_result = ESP_FAIL;
    s_test.spi_free_failures = 1U;
    board_sdspi_config_t config;
    board_sdspi_config_init(&config);
    config.io_expander = &s_tca;

    board_sdspi_t *partial = NULL;
    assert(board_sdspi_mount(&config, &partial) == TEST_CLEANUP_ERROR);
    assert(partial != NULL);
    assert(!board_sdspi_is_mounted(partial));
    assert(s_test.spi_free_calls == 1U);
    assert(s_test.wrapper_remove_calls == 0U);
    assert(board_sdspi_unmount(partial) == ESP_OK);
    assert(s_test.spi_free_calls == 2U);
    assert(s_test.wrapper_remove_calls == 1U);

    s_test.mount_result = ESP_OK;
    board_sdspi_t *device = _test_mount_default();
    assert(board_sdspi_unmount(device) == ESP_OK);
}

static void _test_unmount_cleanup_retry(void)
{
    _test_reset();
    board_sdspi_t *device = _test_mount_default();
    s_test.wrapper_remove_failures = 1U;
    assert(board_sdspi_unmount(device) == TEST_CLEANUP_ERROR);
    assert(!board_sdspi_is_mounted(device));
    assert(s_test.unmount_calls == 1U);
    assert(s_test.spi_free_calls == 1U);
    assert(s_test.wrapper_remove_calls == 1U);

    assert(board_sdspi_unmount(device) == ESP_OK);
    assert(s_test.unmount_calls == 1U);
    assert(s_test.spi_free_calls == 1U);
    assert(s_test.wrapper_remove_calls == 2U);

    board_sdspi_t *replacement = _test_mount_default();
    assert(board_sdspi_unmount(replacement) == ESP_OK);
}

int main(void)
{
    _test_success_and_virtual_cs();
    _test_invalid_filesystem_config();
    _test_mount_rollback_retry();
    _test_unmount_cleanup_retry();
    puts("board SDSPI ownership regression passed");
    return 0;
}

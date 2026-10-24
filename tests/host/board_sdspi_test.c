#include "board_sdspi.h"
#include "driver/sdspi_host.h"
#include "esp_io_expander_gpio_wrapper.h"
#include "esp_vfs_fat.h"
#include "esp_vfs.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "soc/gpio_num.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST_ERROR 0x5501

struct board_tca9554
{
    int unused;
};
static struct board_tca9554 s_tca;
static int s_expander;
static FATFS s_fs;
static sdmmc_card_t *s_card;

typedef enum stage
{
    NONE, DIRECTION, LEVEL, WRAPPER_ADD, BUS_INIT, HOST_INIT, DEVICE_INIT,
    CARD_INIT, DRIVE_ALLOC, VFS_REGISTER, FAT_MOUNT, FAT_UNMOUNT, PARTITION,
    MKFS, CAPACITY, VFS_UNREGISTER, DEVICE_REMOVE, BUS_FREE, WRAPPER_REMOVE,
} stage_t;

static struct
{
    stage_t fail;
    unsigned failures;
    unsigned calls[WRAPPER_REMOVE + 1];
    bool bus;
    bool wrapper;
    bool device;
    bool disk;
    bool vfs;
    bool raw_vfs;
    bool fat;
    bool fat_registered;
    bool zero_capacity;
    bool no_filesystem;
    bool remount_failure;
    unsigned mounts;
    unsigned formats;
    size_t allocation_unit;
} s_test;

static esp_err_t _stage(stage_t stage)
{
    ++s_test.calls[stage];
    if (s_test.fail == stage && s_test.failures != 0U)
    {
        --s_test.failures;
        return TEST_ERROR;
    }
    return ESP_OK;
}

static void _reset(void)
{
    assert(!s_test.bus && !s_test.wrapper && !s_test.device &&
           !s_test.disk && !s_test.vfs && !s_test.raw_vfs && !s_test.fat_registered);
    memset(&s_test, 0, sizeof(s_test));
    s_card = NULL;
}

esp_io_expander_handle_t board_tca9554_get_expander(board_tca9554_t *device)
{
    return device == &s_tca ? &s_expander : NULL;
}

esp_err_t board_tca9554_set_direction(board_tca9554_t *device,
                                      uint8_t mask, bool output)
{
    assert(device == &s_tca && mask == (1U << 7) && output);
    return _stage(DIRECTION);
}

esp_err_t board_tca9554_set_output_level(board_tca9554_t *device,
        uint8_t mask, uint8_t level)
{
    assert(device == &s_tca && mask == (1U << 7) && level == 1U);
    return _stage(LEVEL);
}

esp_err_t esp_io_expander_gpio_wrapper_append_handler(
    esp_io_expander_handle_t handle, uint32_t base)
{
    assert(handle == &s_expander && base == GPIO_NUM_MAX);
    const esp_err_t result = _stage(WRAPPER_ADD);
    s_test.wrapper = result == ESP_OK;
    return result;
}

esp_err_t esp_io_expander_gpio_wrapper_remove_handler(esp_io_expander_handle_t handle)
{
    assert(handle == &s_expander && s_test.wrapper && !s_test.bus);
    const esp_err_t result = _stage(WRAPPER_REMOVE);
    if (result == ESP_OK)
    {
        s_test.wrapper = false;
    }
    return result;
}

esp_err_t spi_bus_initialize(spi_host_device_t host, const spi_bus_config_t *config,
                             spi_dma_chan_t dma)
{
    assert(host == SPI3_HOST && s_test.wrapper && dma == SDSPI_DEFAULT_DMA);
    assert(config->mosi_io_num == GPIO_NUM_1 && config->miso_io_num == GPIO_NUM_3);
    assert(config->sclk_io_num == GPIO_NUM_2);
    const esp_err_t result = _stage(BUS_INIT);
    s_test.bus = result == ESP_OK;
    return result;
}

esp_err_t spi_bus_free(spi_host_device_t host)
{
    assert(host == SPI3_HOST && s_test.bus && !s_test.device);
    const esp_err_t result = _stage(BUS_FREE);
    if (result == ESP_OK)
    {
        s_test.bus = false;
    }
    return result;
}

esp_err_t sdspi_host_init(void)
{
    return _stage(HOST_INIT);
}

esp_err_t sdspi_host_init_device(const sdspi_device_config_t *config,
                                 sdspi_dev_handle_t *handle)
{
    assert(s_test.bus && config->host_id == SPI3_HOST);
    assert(config->gpio_cs == GPIO_NUM_MAX + 7);
    assert(config->gpio_cd == SDSPI_SLOT_NO_CD && config->gpio_wp == SDSPI_SLOT_NO_WP);
    const esp_err_t result = _stage(DEVICE_INIT);
    if (result == ESP_OK)
    {
        s_test.device = true;
        *handle = 42;
    }
    return result;
}

esp_err_t sdspi_host_remove_device(sdspi_dev_handle_t handle)
{
    assert(handle == 42 && s_test.device && !s_test.disk && !s_test.vfs && !s_test.raw_vfs);
    const esp_err_t result = _stage(DEVICE_REMOVE);
    if (result == ESP_OK)
    {
        s_test.device = false;
    }
    return result;
}

esp_err_t sdmmc_card_init(const sdmmc_host_t *host, sdmmc_card_t *card)
{
    assert(host->slot == 42 && host->max_freq_khz == 20000);
    assert(s_test.device);
    s_card = card;
    card->host = *host;
    card->csd.sector_size = 512U;
    return _stage(CARD_INIT);
}

esp_err_t ff_diskio_get_drive(BYTE *drive)
{
    const esp_err_t result = _stage(DRIVE_ALLOC);
    if (result == ESP_OK)
    {
        *drive = 0U;
    }
    return result;
}

void ff_diskio_register_sdmmc(BYTE drive, sdmmc_card_t *card)
{
    assert(drive == 0U && card == s_card && s_test.device && !s_test.disk);
    s_test.disk = true;
}

void ff_sdmmc_set_disk_status_check(BYTE drive, bool enable)
{
    assert(drive == 0U && !enable);
}

void ff_diskio_unregister(BYTE drive)
{
    assert(drive == 0U && s_test.disk && !s_test.fat_registered &&
           !s_test.vfs && !s_test.raw_vfs);
    s_test.disk = false;
}

esp_err_t esp_vfs_fat_register(const esp_vfs_fat_conf_t *config, FATFS **fs)
{
    assert(strcmp(config->base_path, "/sdcard") == 0);
    assert(strcmp(config->fat_drive, "0:") == 0 && config->max_files == 5U);
    assert(s_test.disk && !s_test.vfs);
    const esp_err_t result = _stage(VFS_REGISTER);
    if (result == ESP_OK)
    {
        s_test.vfs = true;
        s_test.raw_vfs = true;
        *fs = &s_fs;
    }
    return result;
}

esp_err_t esp_vfs_fat_unregister_path(const char *path)
{
    assert(strcmp(path, "/sdcard") == 0 && s_test.vfs &&
           !s_test.raw_vfs && !s_test.fat_registered);
    s_test.vfs = false; /* SDK consumes context, raw route already absent. */
    return ESP_ERR_INVALID_STATE;
}

esp_err_t esp_vfs_unregister(const char *path)
{
    assert(strcmp(path, "/sdcard") == 0 && s_test.vfs && s_test.raw_vfs);
    const esp_err_t result = _stage(VFS_UNREGISTER);
    if (result == ESP_OK)
    {
        s_test.raw_vfs = false;
    }
    return result;
}

FRESULT f_mount(FATFS *fs, const char *drive, BYTE immediate)
{
    assert(strcmp(drive, "0:") == 0 && s_test.disk && s_test.vfs);
    if (fs == NULL)
    {
        assert(immediate == 0U);
        if (_stage(FAT_UNMOUNT) != ESP_OK)
        {
            return FR_DISK_ERR;
        }
        s_test.fat = false;
        s_test.fat_registered = false;
        return FR_OK;
    }
    assert(fs == &s_fs && immediate == 1U);
    ++s_test.mounts;
    s_test.fat_registered = true; /* FatFs retains fs even when mount fails. */
    if (_stage(FAT_MOUNT) != ESP_OK || (s_test.remount_failure && s_test.formats != 0U))
    {
        return FR_DISK_ERR;
    }
    if (s_test.no_filesystem && s_test.formats == 0U)
    {
        return FR_NO_FILESYSTEM;
    }
    s_test.fat = true;
    return FR_OK;
}

FRESULT f_fdisk(BYTE drive, const LBA_t partitions[], void *work)
{
    assert(drive == 0U && partitions[0] == 100U && work != NULL);
    assert(!s_test.fat_registered && s_test.disk);
    return _stage(PARTITION) == ESP_OK ? FR_OK : FR_DISK_ERR;
}

FRESULT f_mkfs(const char *drive, const MKFS_PARM *options, void *work, UINT size)
{
    assert(strcmp(drive, "0:") == 0 && !s_test.fat_registered);
    assert(options->fmt == FM_ANY && options->n_fat == 2U);
    assert(work != NULL && size >= 512U);
    s_test.allocation_unit = options->au_size;
    ++s_test.formats;
    return _stage(MKFS) == ESP_OK ? FR_OK : FR_DISK_ERR;
}

esp_err_t esp_vfs_fat_info(const char *path, uint64_t *total, uint64_t *available)
{
    assert(strcmp(path, "/sdcard") == 0 && s_test.fat && s_test.vfs);
    const esp_err_t result = _stage(CAPACITY);
    if (result == ESP_OK)
    {
        *total = s_test.zero_capacity ? 0U : UINT64_C(16) * 1024U * 1024U * 1024U;
        *available = UINT64_C(12) * 1024U * 1024U * 1024U;
    }
    return result;
}

static board_sdspi_t *_mount(bool recover, esp_err_t *result)
{
    board_sdspi_config_t config;
    board_sdspi_config_init(&config);
    config.io_expander = &s_tca;
    config.format_if_mount_failed = recover;
    board_sdspi_t *device = NULL;
    *result = board_sdspi_mount(&config, &device);
    return device;
}

static void _test_mount_stage_failures(void)
{
    const stage_t stages[] = {DIRECTION, LEVEL, WRAPPER_ADD, BUS_INIT, HOST_INIT,
                              DEVICE_INIT, CARD_INIT, DRIVE_ALLOC, VFS_REGISTER, FAT_MOUNT
                             };
    for (size_t i = 0U; i < sizeof(stages) / sizeof(stages[0]); ++i)
    {
        _reset();
        s_test.fail = stages[i];
        s_test.failures = 1U;
        esp_err_t result;
        board_sdspi_t *device = _mount(false, &result);
        assert(result != ESP_OK && device == NULL);
        assert(s_test.formats == 0U);
        _reset();
        device = _mount(false, &result);
        assert(result == ESP_OK && board_sdspi_is_mounted(device));
        assert(board_sdspi_unmount(device) == ESP_OK);
    }
}

static void _test_cleanup_stage_retries(void)
{
    const stage_t stages[] = {FAT_UNMOUNT, VFS_UNREGISTER, DEVICE_REMOVE,
                              BUS_FREE, WRAPPER_REMOVE
                             };
    for (size_t i = 0U; i < sizeof(stages) / sizeof(stages[0]); ++i)
    {
        _reset();
        esp_err_t result;
        board_sdspi_t *device = _mount(false, &result);
        assert(result == ESP_OK);
        s_test.fail = stages[i];
        s_test.failures = 2U;
        assert(board_sdspi_unmount(device) != ESP_OK);
        assert(!board_sdspi_is_mounted(device));
        assert(board_sdspi_unmount(device) != ESP_OK);
        assert(board_sdspi_unmount(device) == ESP_OK);
        assert(s_test.calls[stages[i]] == 3U);
    }
}

static void _test_format_stages(void)
{
    const stage_t stages[] = {FAT_UNMOUNT, MKFS, FAT_MOUNT, CAPACITY};
    for (size_t i = 0U; i < sizeof(stages) / sizeof(stages[0]); ++i)
    {
        _reset();
        esp_err_t result;
        board_sdspi_t *device = _mount(false, &result);
        assert(result == ESP_OK);
        s_test.fail = stages[i];
        s_test.failures = 1U;
        assert(board_sdspi_format(device) != ESP_OK);
        assert(!board_sdspi_is_mounted(device));
        assert(board_sdspi_format(device) == ESP_ERR_INVALID_STATE);
        assert(board_sdspi_unmount(device) == ESP_OK);
    }
    _reset();
    esp_err_t result;
    board_sdspi_t *device = _mount(false, &result);
    assert(result == ESP_OK && board_sdspi_format(device) == ESP_OK);
    assert(s_test.mounts == 2U && s_test.formats == 1U);
    assert(s_test.allocation_unit == 16384U);
    s_test.zero_capacity = true;
    assert(board_sdspi_format(device) == ESP_FAIL);
    assert(!board_sdspi_is_mounted(device));
    assert(board_sdspi_unmount(device) == ESP_OK);
}

static void _test_recovery_only_and_remount_cleanup(void)
{
    _reset();
    s_test.no_filesystem = true;
    esp_err_t result;
    assert(_mount(false, &result) == NULL && result != ESP_OK);
    assert(s_test.formats == 0U);
    _reset();
    s_test.no_filesystem = true;
    board_sdspi_t *device = _mount(true, &result);
    assert(result == ESP_OK && board_sdspi_is_mounted(device));
    assert(s_test.formats == 1U && s_test.calls[PARTITION] == 1U);
    assert(board_sdspi_unmount(device) == ESP_OK);
    _reset();
    s_test.no_filesystem = true;
    s_test.remount_failure = true;
    s_test.fail = VFS_UNREGISTER;
    s_test.failures = 2U;
    device = _mount(true, &result);
    assert(result != ESP_OK && device != NULL);
    assert(!board_sdspi_is_mounted(device));
    assert(board_sdspi_unmount(device) == TEST_ERROR);
    assert(board_sdspi_unmount(device) == ESP_OK);
    _reset();
    s_test.no_filesystem = true;
    s_test.fail = PARTITION;
    s_test.failures = 1U;
    assert(_mount(true, &result) == NULL && result != ESP_OK);
    assert(s_test.formats == 0U);
}

static void _test_configuration_and_capacity(void)
{
    _reset();
    board_sdspi_config_t config;
    board_sdspi_config_init(&config);
    config.io_expander = &s_tca;
    board_sdspi_t *device = NULL;
    assert(!config.format_if_mount_failed);
    config.allocation_unit_size = 1000U;
    assert(board_sdspi_mount(&config, &device) == ESP_ERR_INVALID_ARG);
    config.allocation_unit_size = 16384U;
    memset(config.mount_path, 'x', sizeof(config.mount_path));
    config.mount_path[0] = '/';
    assert(board_sdspi_mount(&config, &device) == ESP_ERR_INVALID_ARG);
    esp_err_t result;
    device = _mount(false, &result);
    assert(result == ESP_OK);
    assert(strcmp(board_sdspi_get_mount_path(device), "/sdcard") == 0);
    uint64_t total = 1U;
    uint64_t available = 1U;
    assert(board_sdspi_get_space(device, &total, &available) == ESP_OK);
    assert(total == UINT64_C(16) * 1024U * 1024U * 1024U);
    s_test.fail = CAPACITY;
    s_test.failures = 1U;
    total = 1U;
    available = 1U;
    assert(board_sdspi_get_space(device, &total, &available) == TEST_ERROR);
    assert(total == 1U && available == 1U);
    assert(board_sdspi_get_space(device, NULL, &available) == ESP_ERR_INVALID_ARG);
    assert(board_sdspi_unmount(device) == ESP_OK);
    assert(board_sdspi_format(NULL) == ESP_ERR_INVALID_STATE);
}

int main(void)
{
    _test_mount_stage_failures();
    _test_cleanup_stage_retries();
    _test_format_stages();
    _test_recovery_only_and_remount_cleanup();
    _test_configuration_and_capacity();
    _reset();
    puts("board SDSPI ownership regression passed");
    return 0;
}

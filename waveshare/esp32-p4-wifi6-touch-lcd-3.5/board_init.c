#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#include "board_init.h"
#include "board_audio.h"
#include "board_power.h"
#include "board_sdmmc.h"
#include "bsp_audio.h"
#include "bsp_hal.h"

#define DBG_TAG "board"
#include "mt_log.h"

static board_context_t s_board;

static bool _screen_is_available(void)
{
    return s_board.initialized && s_board.display.port.panel != NULL &&
           s_board.display.port.panel_io != NULL;
}

static esp_err_t _screen_suspend(void)
{
    return board_display_suspend_impl(&s_board);
}

static esp_err_t _screen_resume_prepare(void)
{
    return board_display_resume_prepare_impl(&s_board);
}

static esp_err_t _screen_resume_commit(void)
{
    return board_display_resume_commit_impl(&s_board);
}

static bool _screen_is_suspended(void)
{
    return s_board.display.power_phase != BOARD_DISPLAY_POWER_PHASE_ENABLED ||
           !s_board.display.enabled;
}

static bool _screen_is_suspend_committed(void)
{
    return _board_display_is_suspend_committed(&s_board.display);
}

static esp_err_t _screen_set_brightness(uint8_t brightness)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (s_board.initialized)
    {
        result = board_display_set_brightness_impl(&s_board, brightness);
        if (result == ESP_OK)
        {
            s_board.settings.brightness = brightness;
        }
    }
    return result;
}

static esp_err_t _screen_set_brightness_temp(uint8_t brightness)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (s_board.initialized)
    {
        result = board_display_set_brightness_impl(&s_board, brightness);
    }
    return result;
}

static uint8_t _screen_get_brightness(void)
{
    return s_board.settings.brightness;
}

static esp_err_t _screen_set_enabled(bool on)
{
    return board_display_set_enabled_impl(&s_board, on);
}

static esp_err_t _screen_set_power(bool on)
{
    return board_display_set_power_impl(&s_board, on);
}

static const bsp_screen_ops_t s_screen_ops =
{
    .is_available = _screen_is_available,
    .suspend = _screen_suspend,
    .resume_prepare = _screen_resume_prepare,
    .resume_commit = _screen_resume_commit,
    .is_suspended = _screen_is_suspended,
    .is_suspend_committed = _screen_is_suspend_committed,
    .set_brightness = _screen_set_brightness,
    .set_brightness_temp = _screen_set_brightness_temp,
    .get_brightness = _screen_get_brightness,
    .set_enabled = _screen_set_enabled,
    .set_power = _screen_set_power,
};

static const bsp_input_ops_t s_input_ops =
{
    .register_handler = board_input_register_handler,
    .unregister_handler = board_input_unregister_handler,
    .prepare_sleep = board_input_prepare_sleep,
    .complete_sleep = board_input_complete_sleep,
};

static esp_err_t _power_poll_irq(uint32_t *status)
{
    if (status == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    board_power_irq_snapshot_t snapshot;
    esp_err_t result = board_power_poll_irq(&snapshot);
    if (result == ESP_OK)
    {
        *status = snapshot.status;
    }
    return result;
}

static const bsp_power_ops_t s_power_ops =
{
    .is_available = board_power_is_available,
    .get_info = board_power_get_info,
    .poll_irq = _power_poll_irq,
};

static bool _audio_is_available(void)
{
    return bsp_audio_is_available();
}

static esp_err_t _audio_configure(const bsp_audio_config_t *config)
{
    return bsp_audio_configure(config);
}

static esp_err_t _audio_start(void)
{
    return bsp_audio_start();
}

static esp_err_t _audio_stop(void)
{
    return bsp_audio_stop();
}

static esp_err_t _audio_write(void *data, size_t size, size_t *written,
                              uint32_t timeout_ms)
{
    return bsp_audio_write(data, size, written, timeout_ms);
}

static esp_err_t _audio_read(void *data, size_t size, size_t *read,
                             uint32_t timeout_ms)
{
    return bsp_audio_read(data, size, read, timeout_ms);
}

static esp_err_t _audio_set_volume(uint8_t volume)
{
    return bsp_audio_set_volume(volume);
}

static uint8_t _audio_get_volume(void)
{
    uint8_t volume = 0U;
    (void)bsp_audio_get_volume(&volume);
    return volume;
}

static esp_err_t _audio_set_mute(bool muted)
{
    return bsp_audio_set_mute(muted);
}

static bool _audio_get_mute(void)
{
    bool muted = false;
    (void)bsp_audio_get_mute(&muted);
    return muted;
}

static esp_err_t _audio_set_pa(bool enabled)
{
    return bsp_audio_set_pa(enabled);
}

static const bsp_audio_ops_t s_audio_ops =
{
    .is_available = _audio_is_available,
    .configure = _audio_configure,
    .start = _audio_start,
    .stop = _audio_stop,
    .write = _audio_write,
    .read = _audio_read,
    .set_volume = _audio_set_volume,
    .get_volume = _audio_get_volume,
    .set_mute = _audio_set_mute,
    .get_mute = _audio_get_mute,
    .set_pa = _audio_set_pa,
};

static bool _sd_is_available(void)
{
    return true;
}

static esp_err_t _sd_mount(const bsp_sd_config_t *config)
{
    if (config == NULL || config->mount_point == NULL ||
            strlen(config->mount_point) >=
            sizeof(((board_sdmmc_config_t *)0)->mount_path))
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!_sd_is_available())
    {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (s_board.sd_card != NULL)
    {
        if (board_sdmmc_is_mounted(s_board.sd_card))
        {
            return strcmp(config->mount_point,
                          board_sdmmc_get_mount_path(s_board.sd_card)) == 0 ?
                   ESP_OK : ESP_ERR_INVALID_STATE;
        }
        esp_err_t result = board_sdmmc_unmount(s_board.sd_card);
        if (result != ESP_OK)
        {
            return result;
        }
        s_board.sd_card = NULL;
    }

    board_sdmmc_config_t sd_config;
    board_sdmmc_config_init(&sd_config);
    (void)snprintf(sd_config.mount_path, sizeof(sd_config.mount_path), "%s",
                   config->mount_point);
    sd_config.format_if_mount_failed = config->format_if_mount_failed;
    sd_config.max_files = config->max_files;
    sd_config.allocation_unit_size = config->allocation_unit_size;
    return board_sdmmc_mount(&sd_config, &s_board.sd_card);
}

static esp_err_t _sd_unmount(void)
{
    if (s_board.sd_card == NULL)
    {
        return ESP_OK;
    }
    esp_err_t result = board_sdmmc_unmount(s_board.sd_card);
    if (result == ESP_OK)
    {
        s_board.sd_card = NULL;
    }
    return result;
}

static bool _sd_is_mounted(void)
{
    return board_sdmmc_is_mounted(s_board.sd_card);
}

static const char *_sd_get_mount_point(void)
{
    return board_sdmmc_get_mount_path(s_board.sd_card);
}

static esp_err_t _sd_format(const bsp_sd_config_t *config)
{
    if (s_board.sd_card == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (config == NULL || config->mount_point == NULL ||
            strlen(config->mount_point) >=
            sizeof(((board_sdmmc_config_t *)0)->mount_path) ||
            strcmp(config->mount_point, _sd_get_mount_point()) != 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return board_sdmmc_format(s_board.sd_card);
}

static esp_err_t _sd_get_space(uint64_t *total_bytes, uint64_t *free_bytes)
{
    return board_sdmmc_get_space(s_board.sd_card, total_bytes, free_bytes);
}

static const bsp_sd_ops_t s_sd_ops =
{
    .is_available = _sd_is_available,
    .mount = _sd_mount,
    .unmount = _sd_unmount,
    .is_mounted = _sd_is_mounted,
    .get_mount_point = _sd_get_mount_point,
    .format = _sd_format,
    .get_space = _sd_get_space,
};

__attribute__((weak)) esp_err_t board_init_stage_gate(board_init_stage_t stage)
{
    (void)stage;
    return ESP_OK;
}

static esp_err_t _board_i2c_init(board_context_t *board)
{
    if (board->i2c_bus != NULL)
    {
        return ESP_OK;
    }
    const i2c_master_bus_config_t bus_config =
    {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = BOARD_I2C_PORT,
        .scl_io_num = BOARD_I2C_PIN_SCL,
        .sda_io_num = BOARD_I2C_PIN_SDA,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = false,
        },
    };
    return i2c_new_master_bus(&bus_config, &board->i2c_bus);
}

static bool _board_has_resources(void)
{
    return s_board.i2c_bus != NULL ||
           board_power_has_resources() ||
           s_board.audio_initialized ||
           s_board.sd_card != NULL ||
           s_board.display.port.panel != NULL ||
           s_board.display.port.panel_io != NULL ||
           s_board.display.port.touch != NULL ||
           s_board.display.port.touch_io != NULL ||
           s_board.display.spi_bus_initialized;
}

static esp_err_t _board_register_interfaces(void)
{
    esp_err_t result = board_init_stage_gate(BOARD_INIT_STAGE_SCREEN_OPS);
    if (result == ESP_OK)
    {
        result = bsp_hal_register_screen(&s_screen_ops);
    }
    if (result != ESP_OK)
    {
        return result;
    }

    result = bsp_hal_register_input(&s_input_ops);
    if (result != ESP_OK)
    {
        return result;
    }

    if ((s_board.capabilities & BSP_CAPABILITY_AUDIO) != 0U)
    {
        result = bsp_hal_register_audio(&s_audio_ops);
        if (result != ESP_OK)
        {
            return result;
        }
    }
    if ((s_board.capabilities & BSP_CAPABILITY_SD) != 0U)
    {
        result = bsp_hal_register_sd(&s_sd_ops);
        if (result != ESP_OK)
        {
            return result;
        }
    }

    const bsp_display_port_t port = s_board.display.port;
    result = board_init_stage_gate(BOARD_INIT_STAGE_DISPLAY_PORT);
    if (result == ESP_OK)
    {
        result = bsp_display_set_port(&port);
    }
    return result;
}

esp_err_t board_init(void)
{
    if (s_board.initialized)
    {
        return ESP_OK;
    }
    if (_board_has_resources())
    {
        const esp_err_t result = board_deinit();
        if (result != ESP_OK)
        {
            return result;
        }
    }

    LOG_I("board init start");
    s_board.capabilities = BSP_CAPABILITY_NONE;
    s_board.settings.brightness = 100U;

    esp_err_t result = board_init_stage_gate(BOARD_INIT_STAGE_I2C);
    if (result == ESP_OK)
    {
        result = _board_i2c_init(&s_board);
    }
    if (result == ESP_OK)
    {
        result = board_init_stage_gate(BOARD_INIT_STAGE_INPUT);
    }
    if (result == ESP_OK)
    {
        result = board_init_stage_gate(BOARD_INIT_STAGE_INPUT_TASK);
    }
    if (result == ESP_OK)
    {
        result = board_input_init();
    }
    if (result == ESP_OK)
    {
        s_board.capabilities |= BSP_CAPABILITY_INPUT;
        result = board_init_stage_gate(BOARD_INIT_STAGE_DISPLAY);
    }
    if (result == ESP_OK)
    {
        result = board_display_init(&s_board);
    }
    if (result == ESP_OK)
    {
        s_board.capabilities |= BSP_CAPABILITY_DISPLAY | BSP_CAPABILITY_TOUCH;
        result = board_init_stage_gate(BOARD_INIT_STAGE_POWER);
    }
    if (result == ESP_OK)
    {
        result = board_power_init(s_board.i2c_bus);
    }
    if (result == ESP_OK)
    {
        s_board.capabilities |= BSP_CAPABILITY_POWER;
        result = bsp_hal_register_power(&s_power_ops);
    }
    else
    {
        /* A missing PMU must not take the panel/touch down; the app degrades
         * its power UI when the capability is absent. */
        LOG_W("power init failed: %s", esp_err_to_name(result));
        (void)board_power_deinit();
        result = ESP_OK;
    }
    if (result == ESP_OK)
    {
        result = board_init_stage_gate(BOARD_INIT_STAGE_AUDIO);
    }
    if (result == ESP_OK)
    {
        result = board_audio_init(s_board.i2c_bus);
    }
    if (result == ESP_OK)
    {
        s_board.audio_initialized = true;
        s_board.capabilities |= BSP_CAPABILITY_AUDIO;
    }
    else
    {
        /* The panel and touch remain usable on an audio-codec fault. */
        LOG_W("audio init failed: %s", esp_err_to_name(result));
        (void)board_audio_deinit();
        s_board.audio_initialized = false;
        result = ESP_OK;
    }
#if defined(MT_BOARD_HAS_SD)
    /* The SD slot is always wired on this board; the card itself is mounted
     * on demand by the storage service. */
    s_board.capabilities |= BSP_CAPABILITY_SD;
#endif
    if (result == ESP_OK)
    {
        result = _board_register_interfaces();
    }
    if (result != ESP_OK)
    {
        const esp_err_t cleanup_result = board_deinit();
        if (cleanup_result != ESP_OK)
        {
            LOG_E("board init rollback failed: %s",
                  esp_err_to_name(cleanup_result));
        }
        return result;
    }

    s_board.initialized = true;
    LOG_I("board init done - screen %dx%d", BOARD_LCD_HOR_RES,
          BOARD_LCD_VER_RES);
    return ESP_OK;
}

esp_err_t board_deinit(void)
{
    s_board.initialized = false;
    s_board.capabilities = BSP_CAPABILITY_NONE;

    esp_err_t first_error = ESP_OK;
    esp_err_t result = board_input_deinit();
    if (result != ESP_OK)
    {
        /* The input task can still invoke the app callback. Keep the display,
         * audio, power and I2C resources alive until the handshake succeeds. */
        return result;
    }
    result = board_display_deinit(&s_board);
    if (first_error == ESP_OK && result != ESP_OK)
    {
        first_error = result;
    }
    if (s_board.audio_initialized)
    {
        result = board_audio_deinit();
        if (first_error == ESP_OK && result != ESP_OK)
        {
            first_error = result;
        }
        if (result == ESP_OK)
        {
            s_board.audio_initialized = false;
        }
    }
    if (s_board.sd_card != NULL)
    {
        result = _sd_unmount();
        if (first_error == ESP_OK && result != ESP_OK)
        {
            first_error = result;
        }
    }
    result = board_power_deinit();
    if (first_error == ESP_OK && result != ESP_OK)
    {
        first_error = result;
    }
    if (s_board.i2c_bus != NULL)
    {
        result = i2c_del_master_bus(s_board.i2c_bus);
        if (first_error == ESP_OK && result != ESP_OK)
        {
            first_error = result;
        }
        if (result == ESP_OK)
        {
            s_board.i2c_bus = NULL;
        }
    }
    return first_error;
}

bsp_capabilities_t board_get_capabilities(void)
{
    return s_board.capabilities;
}

bsp_wakeup_descriptor_t board_get_wakeup_descriptor(void)
{
    const bsp_wakeup_descriptor_t descriptor = {0};
    return descriptor;
}

i2c_master_bus_handle_t board_get_i2c_bus(void)
{
    return s_board.i2c_bus;
}

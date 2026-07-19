#include <stdint.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_io_expander.h"

#include "bsp_hal.h"
#include "board_init.h"
#include "board_tca9554.h"

#define DBG_TAG "board"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#define BOARD_I2C_PORT                (I2C_NUM_0)
#define BOARD_I2C_PIN_SCL             (GPIO_NUM_14)
#define BOARD_I2C_PIN_SDA             (GPIO_NUM_15)
#define BOARD_IO_BOOT_DELAY_MS        (200)
#define BOARD_TCA9554_I2C_ADDRESS     (0x20U)

static board_context_t s_board =
{
    .settings =
    {
        .brightness     = UINT8_MAX,
    },
    .display =
    {
        .brightness = UINT8_MAX,
    },
};

static esp_err_t _board_i2c_init(board_context_t *board)
{
    esp_err_t result = ESP_OK;
    if (board->i2c_bus != NULL)
    {
        return result;
    }

    const i2c_master_bus_config_t i2c_bus_conf =
    {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = BOARD_I2C_PIN_SDA,
        .scl_io_num = BOARD_I2C_PIN_SCL,
        .i2c_port   = BOARD_I2C_PORT,
        .flags      = { .enable_internal_pullup = 1, },
    };

    return i2c_new_master_bus(&i2c_bus_conf, &board->i2c_bus);
}

static esp_err_t _board_io_expander_init(board_context_t *board)
{
    esp_err_t result = board_tca9554_create(board->i2c_bus,
                                            BOARD_TCA9554_I2C_ADDRESS,
                                            &board->io_expander_device);
    if (result != ESP_OK)
    {
        return result;
    }
    board->io_expander = board_tca9554_get_expander(board->io_expander_device);

    result = board_init_stage_gate(BOARD_INIT_STAGE_IO_CONFIG);
    if (result != ESP_OK)
    {
        return result;
    }

    result = esp_io_expander_set_dir(
                 board->io_expander,
                 BOARD_EXIO_PIN_LCD_RESET | BOARD_EXIO_PIN_LCD_PWR_EN |
                 BOARD_EXIO_PIN_TOUCH_RESET,
                 IO_EXPANDER_OUTPUT);
    if (result != ESP_OK)
    {
        return result;
    }

    result = esp_io_expander_set_dir(
                 board->io_expander, BOARD_EXIO_PIN_PWR_BUTTON, IO_EXPANDER_INPUT);
    if (result != ESP_OK)
    {
        return result;
    }

    result = esp_io_expander_set_level(
                 board->io_expander, BOARD_EXIO_PIN_LCD_RESET, 0);
    if (result != ESP_OK)
    {
        return result;
    }

    result = esp_io_expander_set_level(
                 board->io_expander, BOARD_EXIO_PIN_LCD_PWR_EN, 0);
    if (result != ESP_OK)
    {
        return result;
    }

    result = esp_io_expander_set_level(
                 board->io_expander, BOARD_EXIO_PIN_TOUCH_RESET, 0);
    if (result != ESP_OK)
    {
        return result;
    }

    vTaskDelay(pdMS_TO_TICKS(BOARD_IO_BOOT_DELAY_MS));

    result = esp_io_expander_set_level(
                 board->io_expander, BOARD_EXIO_PIN_LCD_RESET, 1);
    if (result != ESP_OK)
    {
        return result;
    }

    result = esp_io_expander_set_level(
                 board->io_expander, BOARD_EXIO_PIN_LCD_PWR_EN, 1);
    if (result != ESP_OK)
    {
        return result;
    }

    return esp_io_expander_set_level(
               board->io_expander, BOARD_EXIO_PIN_TOUCH_RESET, 1);
}

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

__attribute__((weak)) esp_err_t board_init_stage_gate(board_init_stage_t stage)
{
    (void)stage;
    return ESP_OK;
}

bsp_capabilities_t board_get_capabilities(void)
{
    return s_board.capabilities;
}

bsp_wakeup_descriptor_t board_get_wakeup_descriptor(void)
{
    const uint64_t wake_mask = (1ULL << BOARD_HOME_KEY_GPIO);
    const bsp_wakeup_descriptor_t descriptor =
    {
        .gpio_mask = wake_mask,
        .active_low_mask = wake_mask,
    };
    return descriptor;
}

static bool _board_has_resources(void)
{
    return s_board.i2c_bus != NULL ||
           s_board.io_expander_device != NULL ||
           s_board.display.port.panel != NULL ||
           s_board.display.port.panel_io != NULL ||
           s_board.display.port.touch != NULL ||
           s_board.display.port.touch_io != NULL ||
           s_board.display.spi_bus_initialized;
}

static void _board_record_cleanup_error(esp_err_t *first_error, esp_err_t error)
{
    if (*first_error == ESP_OK && error != ESP_OK)
    {
        *first_error = error;
    }
}

static esp_err_t _board_io_expander_deinit(board_context_t *board)
{
    esp_err_t first_error = ESP_OK;
    if (board->io_expander == NULL)
    {
        return first_error;
    }

    esp_err_t result = esp_io_expander_set_level(
                           board->io_expander, BOARD_EXIO_PIN_LCD_RESET, 0);
    _board_record_cleanup_error(&first_error, result);
    result = esp_io_expander_set_level(
                 board->io_expander, BOARD_EXIO_PIN_LCD_PWR_EN, 0);
    _board_record_cleanup_error(&first_error, result);
    result = esp_io_expander_set_level(
                 board->io_expander, BOARD_EXIO_PIN_TOUCH_RESET, 0);
    _board_record_cleanup_error(&first_error, result);
    return first_error;
}

esp_err_t board_deinit(void)
{
    s_board.initialized = false;
    s_board.capabilities = BSP_CAPABILITY_NONE;

    esp_err_t first_error = ESP_OK;
    _board_record_cleanup_error(&first_error, board_power_deinit());
    _board_record_cleanup_error(&first_error, board_rtc_deinit());
    _board_record_cleanup_error(&first_error, board_display_deinit(&s_board));
    esp_err_t input_result = board_input_deinit();
    _board_record_cleanup_error(&first_error, input_result);
    if (input_result != ESP_OK)
    {
        return first_error;
    }
    const bool display_handles_released =
        s_board.display.port.panel == NULL &&
        s_board.display.port.panel_io == NULL &&
        s_board.display.port.touch == NULL &&
        s_board.display.port.touch_io == NULL;
    bool io_outputs_released = false;
    if (display_handles_released)
    {
        esp_err_t ret = _board_io_expander_deinit(&s_board);
        _board_record_cleanup_error(&first_error, ret);
        io_outputs_released = ret == ESP_OK;
    }

    if (s_board.io_expander_device != NULL &&
            display_handles_released && io_outputs_released)
    {
        esp_err_t ret = board_tca9554_destroy(s_board.io_expander_device);
        _board_record_cleanup_error(&first_error, ret);
        if (ret == ESP_OK)
        {
            s_board.io_expander_device = NULL;
            s_board.io_expander = NULL;
        }
    }

    if (s_board.i2c_bus != NULL && s_board.io_expander_device == NULL &&
            s_board.display.port.touch == NULL &&
            s_board.display.port.touch_io == NULL)
    {
        esp_err_t ret = i2c_del_master_bus(s_board.i2c_bus);
        _board_record_cleanup_error(&first_error, ret);
        if (ret == ESP_OK)
        {
            s_board.i2c_bus = NULL;
        }
    }
    return first_error;
}

static esp_err_t _board_init_required_resources(void)
{
    esp_err_t result = board_init_stage_gate(BOARD_INIT_STAGE_I2C);
    if (result == ESP_OK)
    {
        result = _board_i2c_init(&s_board);
    }
    if (result != ESP_OK)
    {
        return result;
    }

    result = board_init_stage_gate(BOARD_INIT_STAGE_IO_EXPANDER);
    if (result == ESP_OK)
    {
        result = _board_io_expander_init(&s_board);
    }
    if (result != ESP_OK)
    {
        return result;
    }

    result = board_init_stage_gate(BOARD_INIT_STAGE_INPUT);
    if (result == ESP_OK)
    {
        result = board_input_init(s_board.io_expander_device);
    }
    if (result != ESP_OK)
    {
        return result;
    }
    s_board.capabilities |= BSP_CAPABILITY_INPUT;

    result = board_init_stage_gate(BOARD_INIT_STAGE_DISPLAY);
    if (result == ESP_OK)
    {
        result = board_display_init(&s_board);
    }
    if (result == ESP_OK)
    {
        s_board.capabilities |= BSP_CAPABILITY_DISPLAY | BSP_CAPABILITY_TOUCH;
    }
    return result;
}

static void _board_init_optional_resources(void)
{
    esp_err_t result = board_init_stage_gate(BOARD_INIT_STAGE_RTC);
    if (result == ESP_OK)
    {
        result = board_rtc_init(s_board.i2c_bus);
    }
    if (result != ESP_OK)
    {
        LOG_W("rtc init failed: %s", esp_err_to_name(result));
    }
    else
    {
        s_board.capabilities |= BSP_CAPABILITY_RTC;
    }

    result = board_init_stage_gate(BOARD_INIT_STAGE_POWER);
    if (result == ESP_OK)
    {
        result = board_power_init(s_board.i2c_bus);
    }
    if (result != ESP_OK)
    {
        LOG_W("power init failed: %s", esp_err_to_name(result));
    }
    else
    {
        s_board.capabilities |= BSP_CAPABILITY_POWER;
    }
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
        LOG_E("screen ops register failed: %s", esp_err_to_name(result));
        return result;
    }

    const bsp_display_port_t port =
    {
        .width = BOARD_LCD_HOR_RES,
        .height = BOARD_LCD_VER_RES,
        .panel = s_board.display.port.panel,
        .panel_io = s_board.display.port.panel_io,
        .touch = s_board.display.port.touch,
        .touch_io = s_board.display.port.touch_io,
    };
    result = board_init_stage_gate(BOARD_INIT_STAGE_DISPLAY_PORT);
    if (result == ESP_OK)
    {
        result = bsp_display_set_port(&port);
    }
    return result;
}

esp_err_t board_init(void)
{
    esp_err_t result = ESP_OK;
    if (s_board.initialized)
    {
        return result;
    }

    if (_board_has_resources())
    {
        result = board_deinit();
        if (result != ESP_OK)
        {
            return result;
        }
    }

    LOG_I("board init start");

    s_board.display.brightness = s_board.settings.brightness;
    s_board.capabilities = BSP_CAPABILITY_NONE;

    result = _board_init_required_resources();
    if (result != ESP_OK)
    {
        goto fail;
    }
    _board_init_optional_resources();
    result = _board_register_interfaces();
    if (result != ESP_OK)
    {
        goto fail;
    }

    s_board.initialized = true;
    LOG_I("board init done - screen registered, display %dx%d",
          BOARD_LCD_HOR_RES, BOARD_LCD_VER_RES);
    return result;

fail:
    {
        esp_err_t cleanup_ret = board_deinit();
        if (cleanup_ret != ESP_OK)
        {
            LOG_E("board init rollback failed: %s", esp_err_to_name(cleanup_ret));
        }
    }
    return result;
}

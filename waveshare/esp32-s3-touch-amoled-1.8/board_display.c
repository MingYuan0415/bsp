#include <stdint.h>
#include <string.h>

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_ft5x06.h"

#include "board_i2c_panel_io.h"
#include "board_init.h"

#define DBG_TAG "board_display"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#define LCD_SPI_HOST                    (SPI2_HOST)
#define LCD_SPI_PIN_PCLK                (GPIO_NUM_11)
#define LCD_SPI_PIN_CS                  (GPIO_NUM_12)
#define LCD_SPI_PIN_RST                 (GPIO_NUM_NC)
#define LCD_SPI_PIN_DATA0               (GPIO_NUM_4)
#define LCD_SPI_PIN_DATA1               (GPIO_NUM_5)
#define LCD_SPI_PIN_DATA2               (GPIO_NUM_6)
#define LCD_SPI_PIN_DATA3               (GPIO_NUM_7)
#define LCD_TE_PIN                       (GPIO_NUM_13)
#define LCD_SPI_PIXEL_CLOCK_HZ          (60 * 1000 * 1000)
#define LCD_SPI_DATA_LINES              (4)
#define LCD_SPI_TRANS_QUEUE_DEPTH       (8)
#define LCD_SPI_MAX_TRANSFER_LINES      (20)

#define LCD_CMD_WRCTRLD                 (0x53)
#define LCD_WRCTRLD_BCTRL_BIT           (0x20)
#define LCD_PWR_RESET_HOLD_MS           (10)
#define LCD_PWR_RESET_RELEASE_MS        (120)
#define LCD_PWR_STABLE_DELAY_MS         (10)
#define LCD_SCANOUT_START_DELAY_MS      (10)
#define LCD_DISP_OFF_SETTLE_MS          (20)
#define LCD_QSPI_OPCODE_WRITE_CMD       (0x02U)
#define LCD_QSPI_CMD(cmd)               ((((cmd) & 0xFF) << 8) | \
                                          (LCD_QSPI_OPCODE_WRITE_CMD << 24))

#define TOUCH_RESET_HOLD_MS             (5)
#define TOUCH_RESET_READY_MS            (300)
#define IO_EXPANDER_WRITE_ATTEMPTS       (3U)
#define IO_EXPANDER_RETRY_DELAY_MS       (2U)
#define FT5X06_REG_CHIP_ID              (0xA3)
#define FT5X06_REG_POWER_MODE           (0xA5)
#define FT5X06_POWER_MODE_HIBERNATE     (3)

#if defined(CONFIG_BSP_DISPLAY_TE_SYNC) && CONFIG_BSP_DISPLAY_TE_SYNC
    #define LCD_TE_SYNC_ENABLED             (true)
#else
    #define LCD_TE_SYNC_ENABLED             (false)
#endif

#if CONFIG_LV_COLOR_DEPTH == 32
    #define LCD_BIT_PER_PIXEL               (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
    #define LCD_BIT_PER_PIXEL               (16)
#endif

#define LCD_SPI_MAX_TRANSFER_SZ         (BOARD_LCD_HOR_RES * \
                                          LCD_SPI_MAX_TRANSFER_LINES * \
                                          LCD_BIT_PER_PIXEL / 8)

static const sh8601_lcd_init_cmd_t s_lcd_init_cmds[] =
{
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x51, (uint8_t[]){0x00}, 1, 10},
};

typedef struct ft5x06_config_entry
{
    uint8_t reg;
    uint8_t value;
} ft5x06_config_entry_t;

static const ft5x06_config_entry_t s_ft5x06_resume_config[] =
{
    {0x80, 70},
    {0x81, 60},
    {0x82, 16},
    {0x83, 60},
    {0x84, 10},
    {0x85, 20},
    {0x87, 2},
    {0x88, 12},
    {0x89, 40},
};

TickType_t board_display_get_tick_count(void);

__attribute__((weak)) TickType_t board_display_get_tick_count(void)
{
    return xTaskGetTickCount();
}

static esp_err_t _board_lcd_tx_param_8b(board_context_t *board, uint8_t cmd,
                                        uint8_t param)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (board != NULL && board->display.port.panel_io != NULL)
    {
        result = esp_lcd_panel_io_tx_param(board->display.port.panel_io,
                                           LCD_QSPI_CMD(cmd), &param,
                                           sizeof(param));
    }
    return result;
}

static esp_err_t _board_touch_write(board_context_t *board,
                                    uint8_t reg,
                                    uint8_t value)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (board != NULL && board->display.port.touch_io != NULL)
    {
        result = esp_lcd_panel_io_tx_param(board->display.port.touch_io,
                                           reg, &value, sizeof(value));
    }
    return result;
}

static esp_err_t _board_display_set_expander_level(board_context_t *board,
        uint32_t pin, uint8_t level)
{
    esp_err_t result = ESP_FAIL;
    for (uint8_t attempt = 0; attempt < IO_EXPANDER_WRITE_ATTEMPTS; ++attempt)
    {
        result = esp_io_expander_set_level(board->io_expander, pin, level);
        if (result == ESP_OK)
        {
            break;
        }
        if (attempt + 1U < IO_EXPANDER_WRITE_ATTEMPTS)
        {
            vTaskDelay(pdMS_TO_TICKS(IO_EXPANDER_RETRY_DELAY_MS));
        }
    }
    return result;
}

static esp_err_t _board_touch_replay_config(board_context_t *board)
{
    esp_err_t result = ESP_OK;
    for (size_t index = 0;
            result == ESP_OK &&
            index < sizeof(s_ft5x06_resume_config) /
            sizeof(s_ft5x06_resume_config[0]); ++index)
    {
        result = _board_touch_write(board,
                                    s_ft5x06_resume_config[index].reg,
                                    s_ft5x06_resume_config[index].value);
    }

    if (result == ESP_OK)
    {
        uint8_t chip_id = 0;
        result = esp_lcd_panel_io_rx_param(board->display.port.touch_io,
                                           FT5X06_REG_CHIP_ID, &chip_id,
                                           sizeof(chip_id));
    }
    return result;
}

static esp_err_t _board_lcd_bus_init(board_context_t *board)
{
    const spi_bus_config_t bus_config = SH8601_PANEL_BUS_QSPI_CONFIG(
                                            LCD_SPI_PIN_PCLK,
                                            LCD_SPI_PIN_DATA0,
                                            LCD_SPI_PIN_DATA1,
                                            LCD_SPI_PIN_DATA2,
                                            LCD_SPI_PIN_DATA3,
                                            LCD_SPI_MAX_TRANSFER_SZ);
    esp_err_t result = board_init_stage_gate(BOARD_INIT_STAGE_SPI_BUS);
    if (result == ESP_OK)
    {
        result = spi_bus_initialize(LCD_SPI_HOST, &bus_config,
                                    SPI_DMA_CH_AUTO);
    }
    if (result == ESP_OK)
    {
        board->display.spi_bus_initialized = true;
    }

    esp_lcd_panel_io_spi_config_t io_config =
        SH8601_PANEL_IO_QSPI_CONFIG(LCD_SPI_PIN_CS, NULL, NULL);
    io_config.pclk_hz = LCD_SPI_PIXEL_CLOCK_HZ;
    io_config.trans_queue_depth = LCD_SPI_TRANS_QUEUE_DEPTH;
    /* Direct PSRAM DMA is reserved for TE's single full-frame path. Bounded
     * 20-line partial transfers reduce the internal bounce allocation from
     * 44 KiB to 14,720 bytes. */
    io_config.flags.psram_dma_direct = LCD_TE_SYNC_ENABLED;

    if (result == ESP_OK)
    {
        result = board_init_stage_gate(BOARD_INIT_STAGE_LCD_IO);
    }
    if (result == ESP_OK)
    {
        result = esp_lcd_new_panel_io_spi(
                     LCD_SPI_HOST, &io_config,
                     &board->display.port.panel_io);
    }
    return result;
}

static esp_err_t _board_lcd_panel_init(board_context_t *board)
{
    sh8601_vendor_config_t vendor_config =
    {
        .init_cmds = s_lcd_init_cmds,
        .init_cmds_size = sizeof(s_lcd_init_cmds) /
        sizeof(s_lcd_init_cmds[0]),
        .flags = { .use_qspi_interface = 1, },
    };

    const esp_lcd_panel_dev_config_t panel_config =
    {
        .reset_gpio_num = LCD_SPI_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };

    esp_err_t result = board_init_stage_gate(BOARD_INIT_STAGE_LCD_PANEL);
    if (result == ESP_OK)
    {
        result = esp_lcd_new_panel_sh8601(board->display.port.panel_io,
                                          &panel_config,
                                          &board->display.port.panel);
    }
    if (result == ESP_OK)
    {
        result = board_init_stage_gate(BOARD_INIT_STAGE_LCD_RESET);
    }
    if (result == ESP_OK)
    {
        result = esp_lcd_panel_reset(board->display.port.panel);
    }
    if (result == ESP_OK)
    {
        result = board_init_stage_gate(BOARD_INIT_STAGE_LCD_INIT);
    }
    if (result == ESP_OK)
    {
        result = esp_lcd_panel_init(board->display.port.panel);
    }
    return result;
}

static esp_err_t _board_lcd_init(board_context_t *board)
{
    esp_err_t result = _board_lcd_bus_init(board);
    if (result == ESP_OK)
    {
        result = _board_lcd_panel_init(board);
    }
    /* SH8601 stays hidden until LVGL fences its first complete frame. */
    return result;
}

static esp_err_t _board_display_start_scanout(board_context_t *board)
{
    if (board == NULL || board->display.port.panel == NULL ||
            !board->display.rail_on || !board->display.panel_initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (board->display.scanout_on)
    {
        return ESP_OK;
    }

    const esp_err_t result = esp_lcd_panel_disp_on_off(
                                 board->display.port.panel, true);
    if (result == ESP_OK)
    {
        board->display.scanout_on = true;
        board->display.brightness_applied = false;
        board->display.enabled = false;
        board->display.power_phase =
            BOARD_DISPLAY_POWER_PHASE_SCANOUT_HIDDEN;
        vTaskDelay(pdMS_TO_TICKS(LCD_SCANOUT_START_DELAY_MS));
    }
    return result;
}

static esp_err_t _board_touch_init(board_context_t *board)
{
    esp_err_t result = ESP_OK;
    if (board->display.port.touch == NULL &&
            board->display.port.touch_io == NULL)
    {
        result = board_init_stage_gate(BOARD_INIT_STAGE_TOUCH_IO);
        if (result == ESP_OK)
        {
            result = board_i2c_panel_io_create(
                         board->i2c_bus,
                         ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS,
                         BOARD_I2C_CLK_HZ,
                         &board->display.port.touch_io);
        }
    }

    const esp_lcd_touch_config_t tp_cfg =
    {
        .x_max = BOARD_LCD_HOR_RES,
        .y_max = BOARD_LCD_VER_RES,
        .rst_gpio_num = BOARD_I2C_PIN_RST,
        .int_gpio_num = BOARD_I2C_PIN_INT,
        .levels = { .reset = 0, .interrupt = 0, },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0, },
    };

    if (result == ESP_OK && board->display.port.touch == NULL)
    {
        result = board_init_stage_gate(BOARD_INIT_STAGE_TOUCH);
    }
    if (result == ESP_OK && board->display.port.touch == NULL)
    {
        result = esp_lcd_touch_new_i2c_ft5x06(
                     board->display.port.touch_io, &tp_cfg,
                     &board->display.port.touch);
    }
    return result;
}

esp_err_t board_display_init(board_context_t *board)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (board == NULL || board->i2c_bus == NULL || board->io_expander == NULL)
    {
        return result;
    }

    board->display.port.width = BOARD_LCD_HOR_RES;
    board->display.port.height = BOARD_LCD_VER_RES;
    board->display.port.te = (bsp_display_te_config_t)
    {
        .enabled = LCD_TE_SYNC_ENABLED,
        .gpio_num = LCD_TE_PIN,
        .bus_freq_hz = LCD_SPI_PIXEL_CLOCK_HZ,
        .data_lines = LCD_SPI_DATA_LINES,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .intr_type = GPIO_INTR_POSEDGE,
    };

    result = _board_lcd_init(board);
    if (result != ESP_OK)
    {
        (void)board_display_deinit(board);
        return result;
    }

    board->display.power_phase = BOARD_DISPLAY_POWER_PHASE_PANEL_INITIALIZED;
    board->display.rail_on = true;
    board->display.reset_released = true;
    board->display.panel_reset = true;
    board->display.panel_initialized = true;
    board->display.scanout_on = false;
    board->display.brightness_applied = false;
    board->display.enabled = false;
    board->display.brightness = board->settings.brightness;

    result = _board_touch_init(board);
    if (result != ESP_OK)
    {
        (void)board_display_deinit(board);
        return result;
    }

    result = gpio_intr_disable(BOARD_I2C_PIN_INT);
    if (result != ESP_OK)
    {
        (void)board_display_deinit(board);
        return result;
    }

    board->display.screen_phase = BOARD_SCREEN_PHASE_TOUCH_CONFIGURED;
    board->display.touch_irq_enabled = false;
    board->display.touch_hibernated = false;
    board->display.touch_reset_released = true;
    board->display.touch_configured = true;

    result = _board_display_start_scanout(board);
    if (result != ESP_OK)
    {
        (void)board_display_deinit(board);
    }
    return result;
}

esp_err_t board_display_set_brightness_impl(board_context_t *board,
        uint8_t brightness)
{
    if (board == NULL || board->display.port.panel_io == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!board->display.rail_on || !board->display.panel_initialized ||
            !board->display.scanout_on || !board->display.enabled)
    {
        board->display.brightness = brightness;
        board->display.brightness_applied = false;
        if (board->display.scanout_on)
        {
            board->display.power_phase =
                BOARD_DISPLAY_POWER_PHASE_SCANOUT_HIDDEN;
        }
        return ESP_OK;
    }

    esp_err_t result = _board_lcd_tx_param_8b(
                           board, LCD_CMD_WRCTRLD,
                           LCD_WRCTRLD_BCTRL_BIT);
    if (result != ESP_OK)
    {
        return result;
    }

    result = _board_lcd_tx_param_8b(board, LCD_CMD_WRDISBV, brightness);
    if (result == ESP_OK)
    {
        board->display.brightness = brightness;
        board->display.brightness_applied = true;
        board->display.power_phase = BOARD_DISPLAY_POWER_PHASE_ENABLED;
    }
    return result;
}

static esp_err_t _board_display_hide_scanout(board_context_t *board)
{
    esp_err_t result = _board_lcd_tx_param_8b(
                           board, LCD_CMD_WRCTRLD,
                           LCD_WRCTRLD_BCTRL_BIT);
    if (result == ESP_OK)
    {
        result = _board_lcd_tx_param_8b(board, LCD_CMD_WRDISBV, 0);
    }
    if (result == ESP_OK)
    {
        board->display.brightness_applied = false;
        board->display.enabled = false;
        board->display.power_phase = board->display.scanout_on ?
                                     BOARD_DISPLAY_POWER_PHASE_SCANOUT_HIDDEN :
                                     BOARD_DISPLAY_POWER_PHASE_PANEL_INITIALIZED;
    }
    return result;
}

esp_err_t board_display_set_enabled_impl(board_context_t *board, bool on)
{
    if (board == NULL || board->display.port.panel == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (on && (!board->display.rail_on ||
               !board->display.panel_initialized ||
               !board->display.scanout_on))
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (board->display.enabled == on)
    {
        return ESP_OK;
    }

    if (!on)
    {
        return _board_display_hide_scanout(board);
    }

    esp_err_t result = _board_lcd_tx_param_8b(
                           board, LCD_CMD_WRCTRLD,
                           LCD_WRCTRLD_BCTRL_BIT);
    if (result == ESP_OK)
    {
        result = _board_lcd_tx_param_8b(
                     board, LCD_CMD_WRDISBV,
                     board->display.brightness);
    }
    if (result == ESP_OK)
    {
        board->display.brightness_applied = true;
        board->display.enabled = true;
        board->display.power_phase = BOARD_DISPLAY_POWER_PHASE_ENABLED;
    }
    else
    {
        (void)_board_display_hide_scanout(board);
    }
    return result;
}

static esp_err_t _board_display_power_rail_on(board_context_t *board)
{
    esp_err_t result = _board_display_set_expander_level(
                           board, BOARD_EXIO_PIN_LCD_PWR_EN, 1);
    if (result == ESP_OK)
    {
        board->display.rail_on = true;
        board->display.reset_released = false;
        board->display.panel_reset = false;
        board->display.panel_initialized = false;
        board->display.scanout_on = false;
        board->display.brightness_applied = false;
        board->display.enabled = false;
        board->display.power_phase = BOARD_DISPLAY_POWER_PHASE_RAIL_ON;
        vTaskDelay(pdMS_TO_TICKS(LCD_PWR_STABLE_DELAY_MS));
    }
    return result;
}

static esp_err_t _board_display_assert_reset(board_context_t *board)
{
    esp_err_t result = _board_display_set_expander_level(
                           board, BOARD_EXIO_PIN_LCD_RESET, 0);
    if (result == ESP_OK)
    {
        board->display.reset_released = false;
        board->display.panel_reset = false;
        board->display.panel_initialized = false;
        board->display.scanout_on = false;
        board->display.brightness_applied = false;
        board->display.enabled = false;
        board->display.power_phase = BOARD_DISPLAY_POWER_PHASE_RESET_ASSERTED;
        vTaskDelay(pdMS_TO_TICKS(LCD_PWR_RESET_HOLD_MS));
    }
    return result;
}

static esp_err_t _board_display_release_reset(board_context_t *board)
{
    esp_err_t result = _board_display_set_expander_level(
                           board, BOARD_EXIO_PIN_LCD_RESET, 1);
    if (result == ESP_OK)
    {
        board->display.reset_released = true;
        board->display.power_phase =
            BOARD_DISPLAY_POWER_PHASE_RESET_RELEASED;
        vTaskDelay(pdMS_TO_TICKS(LCD_PWR_RESET_RELEASE_MS));
    }
    return result;
}

static esp_err_t _board_display_reset_panel(board_context_t *board)
{
    esp_err_t result = esp_lcd_panel_reset(board->display.port.panel);
    if (result == ESP_OK)
    {
        board->display.panel_reset = true;
        board->display.power_phase = BOARD_DISPLAY_POWER_PHASE_PANEL_RESET;
    }
    return result;
}

static esp_err_t _board_display_init_panel(board_context_t *board)
{
    esp_err_t result = esp_lcd_panel_init(board->display.port.panel);
    if (result == ESP_OK)
    {
        board->display.panel_initialized = true;
        board->display.scanout_on = false;
        board->display.brightness_applied = false;
        board->display.enabled = false;
        board->display.power_phase =
            BOARD_DISPLAY_POWER_PHASE_PANEL_INITIALIZED;
    }
    return result;
}

static esp_err_t _board_display_prepare_power_impl(board_context_t *board)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (board == NULL || board->io_expander == NULL ||
            board->display.port.panel == NULL)
    {
        return result;
    }

    result = ESP_OK;
    if (board->display.power_phase == BOARD_DISPLAY_POWER_PHASE_OFF)
    {
        result = _board_display_power_rail_on(board);
    }
    if (result == ESP_OK &&
            board->display.power_phase == BOARD_DISPLAY_POWER_PHASE_RAIL_ON)
    {
        result = _board_display_assert_reset(board);
    }
    if (result == ESP_OK && board->display.power_phase ==
            BOARD_DISPLAY_POWER_PHASE_RESET_ASSERTED)
    {
        result = _board_display_release_reset(board);
    }
    if (result == ESP_OK && board->display.power_phase ==
            BOARD_DISPLAY_POWER_PHASE_RESET_RELEASED)
    {
        result = _board_display_reset_panel(board);
    }
    if (result == ESP_OK && board->display.power_phase ==
            BOARD_DISPLAY_POWER_PHASE_PANEL_RESET)
    {
        result = _board_display_init_panel(board);
    }
    if (result == ESP_OK && board->display.power_phase ==
            BOARD_DISPLAY_POWER_PHASE_PANEL_INITIALIZED)
    {
        result = _board_display_start_scanout(board);
    }

    if (result == ESP_OK &&
            board->display.power_phase != BOARD_DISPLAY_POWER_PHASE_ENABLED &&
            board->display.power_phase !=
            BOARD_DISPLAY_POWER_PHASE_SCANOUT_HIDDEN)
    {
        result = ESP_ERR_INVALID_STATE;
    }
    return result;
}

static void _board_display_restore_after_off_failure(board_context_t *board)
{
    if (board->display.touch_hibernated ||
            !board->display.touch_reset_released)
    {
        esp_err_t result = _board_display_set_expander_level(
                               board, BOARD_EXIO_PIN_TOUCH_RESET, 0);
        if (result == ESP_OK)
        {
            board->display.touch_reset_released = false;
            board->display.touch_hibernated = false;
            board->display.touch_configured = false;
            vTaskDelay(pdMS_TO_TICKS(TOUCH_RESET_HOLD_MS));
            result = _board_display_set_expander_level(
                         board, BOARD_EXIO_PIN_TOUCH_RESET, 1);
        }
        if (result == ESP_OK)
        {
            board->display.touch_reset_released = true;
            vTaskDelay(pdMS_TO_TICKS(TOUCH_RESET_READY_MS));
            result = _board_touch_replay_config(board);
        }
        if (result == ESP_OK)
        {
            board->display.touch_configured = true;
        }
        else
        {
            LOG_E("touch restore after panel failure: %#x",
                  (unsigned int)result);
        }
    }

    if (!board->display.touch_hibernated &&
            board->display.touch_reset_released &&
            board->display.touch_configured &&
            !board->display.touch_irq_enabled && board->display.rail_on &&
            board->display.panel_initialized && board->display.enabled)
    {
        if (gpio_intr_enable(BOARD_I2C_PIN_INT) == ESP_OK)
        {
            board->display.touch_irq_enabled = true;
        }
    }

    if (board->display.touch_irq_enabled && board->display.enabled)
    {
        board->display.screen_phase = BOARD_SCREEN_PHASE_ACTIVE;
    }
}

static void _board_display_mark_power_off(board_context_t *board)
{
    board->display.rail_on = false;
    board->display.reset_released = false;
    board->display.panel_reset = false;
    board->display.panel_initialized = false;
    board->display.scanout_on = false;
    board->display.brightness_applied = false;
    board->display.enabled = false;
    board->display.power_phase = BOARD_DISPLAY_POWER_PHASE_OFF;
}

static esp_err_t _board_display_stop_scanout(board_context_t *board)
{
    if (!board->display.scanout_on)
    {
        return ESP_OK;
    }

    const esp_err_t result = esp_lcd_panel_disp_on_off(
                                 board->display.port.panel, false);
    if (result == ESP_OK)
    {
        board->display.scanout_on = false;
        board->display.brightness_applied = false;
        board->display.enabled = false;
        board->display.power_phase =
            BOARD_DISPLAY_POWER_PHASE_PANEL_INITIALIZED;
    }
    return result;
}

static esp_err_t _board_display_power_off(board_context_t *board)
{
    esp_err_t result = ESP_OK;
    if (board->display.power_phase == BOARD_DISPLAY_POWER_PHASE_OFF &&
            !board->display.rail_on && !board->display.scanout_on)
    {
        board->display.enabled = false;
        return result;
    }

    result = _board_display_stop_scanout(board);
    if (result != ESP_OK)
    {
        /* DISP_OFF failure must not leave the input side hibernated. */
        _board_display_restore_after_off_failure(board);
        return result;
    }

    if (board->display.rail_on &&
            board->display.power_phase !=
            BOARD_DISPLAY_POWER_PHASE_RESET_ASSERTED)
    {
        vTaskDelay(pdMS_TO_TICKS(LCD_DISP_OFF_SETTLE_MS));
        result = _board_display_assert_reset(board);
    }

    if (result == ESP_OK && board->display.rail_on)
    {
        result = _board_display_set_expander_level(
                     board, BOARD_EXIO_PIN_LCD_PWR_EN, 0);
    }
    if (result == ESP_OK)
    {
        _board_display_mark_power_off(board);
    }
    return result;
}

esp_err_t board_display_set_power_impl(board_context_t *board, bool on)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (board == NULL || board->io_expander == NULL ||
            board->display.port.panel == NULL)
    {
        return result;
    }

    if (on)
    {
        result = _board_display_prepare_power_impl(board);
        if (result == ESP_OK)
        {
            result = board_display_set_enabled_impl(board, true);
        }
    }
    else
    {
        result = _board_display_power_off(board);
    }
    return result;
}

static void _board_display_restore_suspend_irq(board_context_t *board,
        bool restore_active_irq)
{
    if (restore_active_irq)
    {
        const esp_err_t result = gpio_intr_enable(BOARD_I2C_PIN_INT);
        /* Force the next suspend attempt to disable an uncertain IRQ state. */
        board->display.touch_irq_enabled = true;
        if (result == ESP_OK)
        {
            board->display.screen_phase = BOARD_SCREEN_PHASE_ACTIVE;
        }
    }
}

static esp_err_t _board_display_suspend_touch(board_context_t *board,
        bool restore_active_irq)
{
    esp_err_t result = ESP_OK;
    if (board->display.touch_irq_enabled)
    {
        result = gpio_intr_disable(BOARD_I2C_PIN_INT);
        if (result == ESP_OK)
        {
            board->display.touch_irq_enabled = false;
        }
    }
    if (result == ESP_OK)
    {
        board->display.screen_phase = BOARD_SCREEN_PHASE_SUSPENDING;
    }

    if (result == ESP_OK && board->display.touch_reset_released &&
            !board->display.touch_hibernated)
    {
        if (board->display.touch_configured)
        {
            result = _board_touch_write(board, FT5X06_REG_POWER_MODE,
                                        FT5X06_POWER_MODE_HIBERNATE);
            if (result == ESP_OK)
            {
                board->display.touch_hibernated = true;
                board->display.touch_configured = false;
            }
            else
            {
                const esp_err_t hibernate_result = result;
                /* Some FT5X06 revisions stop acknowledging after accepting
                 * HIBERNATE. Reset is the deterministic suspend fallback. */
                if (hibernate_result != ESP_ERR_INVALID_RESPONSE)
                {
                    LOG_W("touch hibernate failed, use reset: %#x",
                          (unsigned int)hibernate_result);
                }
                result = _board_display_set_expander_level(
                             board, BOARD_EXIO_PIN_TOUCH_RESET, 0);
                if (result == ESP_OK)
                {
                    board->display.touch_reset_released = false;
                    board->display.touch_hibernated = false;
                    board->display.touch_configured = false;
                }
                else
                {
                    LOG_E("touch reset fallback failed: %#x",
                          (unsigned int)result);
                    _board_display_restore_suspend_irq(
                        board, restore_active_irq);
                }
            }
        }
        else
        {
            result = _board_display_set_expander_level(
                         board, BOARD_EXIO_PIN_TOUCH_RESET, 0);
            if (result == ESP_OK)
            {
                board->display.touch_reset_released = false;
                board->display.touch_hibernated = false;
                board->display.touch_configured = false;
            }
        }
    }
    return result;
}

esp_err_t board_display_suspend_impl(board_context_t *board)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (board == NULL || board->io_expander == NULL ||
            board->display.port.panel == NULL ||
            board->display.port.touch_io == NULL)
    {
        return result;
    }

    if (_board_display_is_suspend_committed(&board->display))
    {
        return ESP_OK;
    }

    const bool restore_active_irq =
        board->display.screen_phase == BOARD_SCREEN_PHASE_ACTIVE &&
        board->display.enabled &&
        board->display.power_phase == BOARD_DISPLAY_POWER_PHASE_ENABLED &&
        board->display.touch_irq_enabled;
    result = _board_display_suspend_touch(board, restore_active_irq);
    if (result != ESP_OK)
    {
        LOG_E("suspend touch failed: %#x", (unsigned int)result);
    }
    if (result == ESP_OK)
    {
        result = board_display_set_power_impl(board, false);
        if (result != ESP_OK)
        {
            LOG_E("suspend panel power-off failed: %#x",
                  (unsigned int)result);
        }
    }
    if (result == ESP_OK)
    {
        board->display.screen_phase = BOARD_SCREEN_PHASE_SUSPENDED;
    }
    return result;
}

static void _board_display_hide_after_commit_failure(board_context_t *board,
        bool irq_state_unknown)
{
    if (irq_state_unknown)
    {
        board->display.touch_irq_enabled = true;
        if (gpio_intr_disable(BOARD_I2C_PIN_INT) == ESP_OK)
        {
            board->display.touch_irq_enabled = false;
        }
    }

    if (board->display.rail_on && board->display.panel_initialized &&
            board->display.scanout_on)
    {
        if (board->display.enabled || board->display.brightness_applied)
        {
            (void)_board_display_hide_scanout(board);
        }
        (void)_board_display_stop_scanout(board);
    }
}

static esp_err_t _board_display_reset_touch(board_context_t *board,
        TickType_t *released_at)
{
    esp_err_t result = _board_display_set_expander_level(
                           board, BOARD_EXIO_PIN_TOUCH_RESET, 0);
    if (result == ESP_OK)
    {
        board->display.touch_reset_released = false;
        board->display.touch_hibernated = false;
        board->display.touch_configured = false;
        board->display.screen_phase =
            BOARD_SCREEN_PHASE_TOUCH_RESET_ASSERTED;
        vTaskDelay(pdMS_TO_TICKS(TOUCH_RESET_HOLD_MS));
    }

    if (result == ESP_OK)
    {
        result = _board_display_set_expander_level(
                     board, BOARD_EXIO_PIN_TOUCH_RESET, 1);
    }
    if (result == ESP_OK)
    {
        board->display.touch_reset_released = true;
        board->display.screen_phase =
            BOARD_SCREEN_PHASE_TOUCH_RESET_RELEASED;
        *released_at = board_display_get_tick_count();
    }
    return result;
}

static esp_err_t _board_display_finish_resume_prepare(
    board_context_t *board, TickType_t touch_released_at)
{
    esp_err_t result = _board_display_prepare_power_impl(board);
    if (result == ESP_OK)
    {
        board->display.screen_phase = BOARD_SCREEN_PHASE_PANEL_PREPARED;
    }

    if (result == ESP_OK)
    {
        const TickType_t ready_ticks = pdMS_TO_TICKS(TOUCH_RESET_READY_MS);
        const TickType_t elapsed = board_display_get_tick_count() -
                                   touch_released_at;
        if (elapsed < ready_ticks)
        {
            vTaskDelay(ready_ticks - elapsed);
        }
        result = _board_touch_replay_config(board);
    }
    if (result == ESP_OK)
    {
        board->display.touch_configured = true;
        board->display.screen_phase = BOARD_SCREEN_PHASE_TOUCH_CONFIGURED;
    }
    return result;
}

esp_err_t board_display_resume_prepare_impl(board_context_t *board)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (board == NULL || board->io_expander == NULL ||
            board->display.port.panel == NULL ||
            board->display.port.touch_io == NULL)
    {
        return result;
    }

    if (board->display.screen_phase == BOARD_SCREEN_PHASE_TOUCH_CONFIGURED &&
            board->display.power_phase ==
            BOARD_DISPLAY_POWER_PHASE_SCANOUT_HIDDEN &&
            board->display.scanout_on && !board->display.enabled &&
            !board->display.brightness_applied &&
            !board->display.touch_irq_enabled)
    {
        return ESP_OK;
    }

    result = board_display_suspend_impl(board);
    TickType_t touch_released_at = 0;
    if (result == ESP_OK)
    {
        /* Preserve partial phases until app-manager enters its barrier. */
        result = _board_display_reset_touch(board, &touch_released_at);
    }
    if (result == ESP_OK)
    {
        result = _board_display_finish_resume_prepare(
                     board, touch_released_at);
    }
    return result;
}

esp_err_t board_display_resume_commit_impl(board_context_t *board)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (board == NULL || board->display.port.panel == NULL)
    {
        return result;
    }

    if (board->display.screen_phase == BOARD_SCREEN_PHASE_ACTIVE &&
            board->display.scanout_on && board->display.enabled &&
            board->display.touch_irq_enabled)
    {
        return ESP_OK;
    }
    if (board->display.screen_phase != BOARD_SCREEN_PHASE_TOUCH_CONFIGURED ||
            board->display.power_phase !=
            BOARD_DISPLAY_POWER_PHASE_SCANOUT_HIDDEN ||
            !board->display.scanout_on || board->display.enabled ||
            board->display.brightness_applied ||
            board->display.touch_irq_enabled)
    {
        return result;
    }

    result = board_display_set_enabled_impl(board, true);
    if (result != ESP_OK)
    {
        _board_display_hide_after_commit_failure(board, false);
        return result;
    }

    result = gpio_intr_enable(BOARD_I2C_PIN_INT);
    if (result != ESP_OK)
    {
        _board_display_hide_after_commit_failure(board, true);
        return result;
    }
    board->display.touch_irq_enabled = true;
    board->display.screen_phase = BOARD_SCREEN_PHASE_ACTIVE;
    return result;
}

static void _board_display_record_first_error(esp_err_t *first_error,
        esp_err_t result)
{
    if (*first_error == ESP_OK && result != ESP_OK)
    {
        *first_error = result;
    }
}

static esp_err_t _board_display_deinit_touch(board_context_t *board)
{
    esp_err_t first_error = ESP_OK;
    if (board->display.port.touch != NULL)
    {
        const esp_err_t result = esp_lcd_touch_del(
                                     board->display.port.touch);
        if (result == ESP_OK)
        {
            board->display.port.touch = NULL;
        }
        _board_display_record_first_error(&first_error, result);
    }

    if (board->display.port.touch_io != NULL &&
            board->display.port.touch == NULL)
    {
        const esp_err_t result = esp_lcd_panel_io_del(
                                     board->display.port.touch_io);
        if (result == ESP_OK)
        {
            board->display.port.touch_io = NULL;
        }
        _board_display_record_first_error(&first_error, result);
    }
    return first_error;
}

static esp_err_t _board_display_deinit_panel(board_context_t *board)
{
    esp_err_t first_error = ESP_OK;
    if (board->display.port.panel != NULL)
    {
        const esp_err_t result = esp_lcd_panel_del(board->display.port.panel);
        if (result == ESP_OK)
        {
            board->display.port.panel = NULL;
        }
        _board_display_record_first_error(&first_error, result);
    }

    if (board->display.port.panel_io != NULL &&
            board->display.port.panel == NULL)
    {
        const esp_err_t result = esp_lcd_panel_io_del(
                                     board->display.port.panel_io);
        if (result == ESP_OK)
        {
            board->display.port.panel_io = NULL;
        }
        _board_display_record_first_error(&first_error, result);
    }
    return first_error;
}

static esp_err_t _board_display_deinit_spi(board_context_t *board)
{
    esp_err_t result = ESP_OK;
    if (board->display.spi_bus_initialized &&
            board->display.port.panel == NULL &&
            board->display.port.panel_io == NULL)
    {
        result = spi_bus_free(LCD_SPI_HOST);
        if (result == ESP_OK)
        {
            board->display.spi_bus_initialized = false;
        }
    }
    return result;
}

esp_err_t board_display_deinit(board_context_t *board)
{
    esp_err_t first_error = ESP_ERR_INVALID_ARG;
    if (board == NULL)
    {
        return first_error;
    }

    first_error = ESP_OK;
    if (board->display.port.panel != NULL && board->io_expander != NULL &&
            (board->display.rail_on ||
             board->display.scanout_on ||
             board->display.power_phase != BOARD_DISPLAY_POWER_PHASE_OFF))
    {
        first_error = board_display_set_power_impl(board, false);
        if (first_error != ESP_OK)
        {
            return first_error;
        }
    }

    esp_err_t result = _board_display_deinit_touch(board);
    _board_display_record_first_error(&first_error, result);
    result = _board_display_deinit_panel(board);
    _board_display_record_first_error(&first_error, result);
    result = _board_display_deinit_spi(board);
    _board_display_record_first_error(&first_error, result);

    if (first_error == ESP_OK)
    {
        memset(&board->display, 0, sizeof(board->display));
    }
    return first_error;
}

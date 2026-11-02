#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_lcd_touch_ft5x06.h"

#include "board_init.h"

#define DBG_TAG "board_display"
#include "mt_log.h"

#define BOARD_LCD_SPI_HOST              (SPI2_HOST)
#define BOARD_LCD_PIN_MOSI              (GPIO_NUM_20)
#define BOARD_LCD_PIN_SCLK              (GPIO_NUM_21)
#define BOARD_LCD_PIN_CS                (GPIO_NUM_23)
#define BOARD_LCD_PIN_DC                (GPIO_NUM_26)
#define BOARD_LCD_PIN_RST               (GPIO_NUM_27)
#define BOARD_LCD_PIN_BL                (GPIO_NUM_28)
#define BOARD_TOUCH_PIN_RST             (GPIO_NUM_29)
#define BOARD_TOUCH_PIN_INT             (GPIO_NUM_50)

#define BOARD_LCD_LEDC_MODE             (LEDC_LOW_SPEED_MODE)
#define BOARD_LCD_LEDC_TIMER            (LEDC_TIMER_0)
#define BOARD_LCD_LEDC_CHANNEL          (LEDC_CHANNEL_0)
#define BOARD_LCD_LEDC_FREQ_HZ          (5000U)
#define BOARD_LCD_LEDC_DUTY_MAX         (1023U)
#define BOARD_BRIGHTNESS_DEFAULT        (100U)

#define BOARD_LCD_MAX_TRANSFER_SZ       (BOARD_LCD_HOR_RES * \
                                          BOARD_LCD_SPI_MAX_TRANSFER_LINES * \
                                          BOARD_LCD_BITS_PER_PIXEL / 8)

static esp_err_t _board_spi_bus_init(void)
{
    const spi_bus_config_t bus_config =
    {
        .mosi_io_num = BOARD_LCD_PIN_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = BOARD_LCD_PIN_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = BOARD_LCD_MAX_TRANSFER_SZ,
    };
    return spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus_config,
                              SPI_DMA_CH_AUTO);
}

static esp_err_t _board_panel_init(board_context_t *board)
{
    const esp_lcd_panel_io_spi_config_t io_config =
    {
        .dc_gpio_num = BOARD_LCD_PIN_DC,
        .cs_gpio_num = BOARD_LCD_PIN_CS,
        .pclk_hz = BOARD_LCD_SPI_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = BOARD_LCD_SPI_TRANS_QUEUE_DEPTH,
        .flags = {
            .psram_dma_direct = BOARD_LCD_PSRAM_DMA_DIRECT,
        },
    };
    esp_err_t result = esp_lcd_new_panel_io_spi(
                           (esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST,
                           &io_config, &board->display.port.panel_io);
    if (result != ESP_OK)
    {
        return result;
    }

    const esp_lcd_panel_dev_config_t panel_config =
    {
        .reset_gpio_num = BOARD_LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = BOARD_LCD_BITS_PER_PIXEL,
    };
    result = esp_lcd_new_panel_st7796(board->display.port.panel_io,
                                      &panel_config,
                                      &board->display.port.panel);
    if (result != ESP_OK)
    {
        return result;
    }

    result = esp_lcd_panel_reset(board->display.port.panel);
    if (result != ESP_OK)
    {
        return result;
    }
    result = esp_lcd_panel_init(board->display.port.panel);
    if (result != ESP_OK)
    {
        return result;
    }
    result = esp_lcd_panel_invert_color(board->display.port.panel, true);
    if (result != ESP_OK)
    {
        return result;
    }
    result = esp_lcd_panel_mirror(board->display.port.panel, true, false);
    if (result != ESP_OK)
    {
        return result;
    }
    return esp_lcd_panel_disp_on_off(board->display.port.panel, false);
}

static esp_err_t _board_backlight_init(void)
{
    const ledc_timer_config_t timer_config =
    {
        .speed_mode = BOARD_LCD_LEDC_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BOARD_LCD_LEDC_TIMER,
        .freq_hz = BOARD_LCD_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t result = ledc_timer_config(&timer_config);
    if (result != ESP_OK)
    {
        return result;
    }

    const ledc_channel_config_t channel_config =
    {
        .gpio_num = BOARD_LCD_PIN_BL,
        .speed_mode = BOARD_LCD_LEDC_MODE,
        .channel = BOARD_LCD_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BOARD_LCD_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&channel_config);
}

static void _board_backlight_deinit(void)
{
    (void)ledc_stop(BOARD_LCD_LEDC_MODE, BOARD_LCD_LEDC_CHANNEL, 0);
    const ledc_timer_config_t timer_config =
    {
        .speed_mode = BOARD_LCD_LEDC_MODE,
        .timer_num = BOARD_LCD_LEDC_TIMER,
        .deconfigure = 1,
    };
    (void)ledc_timer_pause(BOARD_LCD_LEDC_MODE, BOARD_LCD_LEDC_TIMER);
    (void)ledc_timer_config(&timer_config);
}

static esp_err_t _board_apply_brightness(board_context_t *board,
        uint8_t brightness)
{
    if (brightness > 100U)
    {
        brightness = 100U;
    }
    const uint32_t duty = (BOARD_LCD_LEDC_DUTY_MAX * brightness) / 100U;
    esp_err_t result = ledc_set_duty(BOARD_LCD_LEDC_MODE,
                                     BOARD_LCD_LEDC_CHANNEL, duty);
    if (result == ESP_OK)
    {
        result = ledc_update_duty(BOARD_LCD_LEDC_MODE,
                                  BOARD_LCD_LEDC_CHANNEL);
    }
    if (result == ESP_OK)
    {
        board->display.brightness = brightness;
        board->display.brightness_applied = brightness > 0U;
    }
    return result;
}

static esp_err_t _board_touch_init(board_context_t *board)
{
    esp_lcd_panel_io_i2c_config_t io_config =
        ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io_config.scl_speed_hz = BOARD_I2C_CLK_HZ;
    esp_err_t result = esp_lcd_new_panel_io_i2c(board->i2c_bus, &io_config,
        &board->display.port.touch_io);
    if (result != ESP_OK)
    {
        return result;
    }

    const esp_lcd_touch_config_t touch_config =
    {
        .x_max = BOARD_LCD_HOR_RES,
        .y_max = BOARD_LCD_VER_RES,
        .rst_gpio_num = BOARD_TOUCH_PIN_RST,
        .int_gpio_num = BOARD_TOUCH_PIN_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    result = esp_lcd_touch_new_i2c_ft5x06(board->display.port.touch_io,
                                          &touch_config,
                                          &board->display.port.touch);
    if (result != ESP_OK)
    {
        return result;
    }
    return gpio_intr_disable(BOARD_TOUCH_PIN_INT);
}

static void _board_display_clear(board_context_t *board)
{
    if (board->display.port.touch != NULL)
    {
        (void)esp_lcd_touch_del(board->display.port.touch);
        board->display.port.touch = NULL;
    }
    if (board->display.port.touch_io != NULL)
    {
        (void)esp_lcd_panel_io_del(board->display.port.touch_io);
        board->display.port.touch_io = NULL;
    }
    if (board->display.port.panel != NULL)
    {
        (void)esp_lcd_panel_del(board->display.port.panel);
        board->display.port.panel = NULL;
    }
    if (board->display.port.panel_io != NULL)
    {
        (void)esp_lcd_panel_io_del(board->display.port.panel_io);
        board->display.port.panel_io = NULL;
    }
    if (board->display.spi_bus_initialized)
    {
        (void)spi_bus_free(BOARD_LCD_SPI_HOST);
        board->display.spi_bus_initialized = false;
    }
    board->display.panel_initialized = false;
    board->display.scanout_on = false;
    board->display.enabled = false;
    board->display.brightness_applied = false;
    board->display.touch_irq_enabled = false;
    board->display.touch_configured = false;
    board->display.power_phase = BOARD_DISPLAY_POWER_PHASE_OFF;
}

esp_err_t board_display_init(board_context_t *board)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (board == NULL || board->i2c_bus == NULL)
    {
        return result;
    }

    board->display.port.width = BOARD_LCD_HOR_RES;
    board->display.port.height = BOARD_LCD_VER_RES;
    board->display.port.transport = (bsp_display_transport_t)
    {
        .kind = BSP_DISPLAY_TRANSPORT_SPI,
        .clock_hz = BOARD_LCD_SPI_CLOCK_HZ,
        .max_transfer_lines = BOARD_LCD_SPI_MAX_TRANSFER_LINES,
        .dma_max_full_lines = BOARD_LCD_DMA_MAX_FULL_LINES,
        .transaction_queue_depth = BOARD_LCD_SPI_TRANS_QUEUE_DEPTH,
        .data_lines = BOARD_LCD_SPI_DATA_LINES,
        .bits_per_pixel = BOARD_LCD_BITS_PER_PIXEL,
        .psram_dma_direct = BOARD_LCD_PSRAM_DMA_DIRECT,
    };
    board->display.port.te = (bsp_display_te_config_t)
    {
        .enabled = false,
        .gpio_num = -1,
        .intr_type = GPIO_INTR_DISABLE,
    };

    result = board_init_stage_gate(BOARD_INIT_STAGE_SPI_BUS);
    if (result == ESP_OK)
    {
        result = _board_spi_bus_init();
    }
    if (result == ESP_OK)
    {
        board->display.spi_bus_initialized = true;
        result = board_init_stage_gate(BOARD_INIT_STAGE_LCD_PANEL);
    }
    if (result == ESP_OK)
    {
        result = _board_panel_init(board);
    }
    if (result == ESP_OK)
    {
        result = _board_backlight_init();
    }
    if (result == ESP_OK)
    {
        result = board_init_stage_gate(BOARD_INIT_STAGE_TOUCH);
    }
    if (result == ESP_OK)
    {
        result = _board_touch_init(board);
    }
    if (result != ESP_OK)
    {
        board_display_deinit(board);
        return result;
    }

    board->display.panel_initialized = true;
    board->display.touch_configured = true;
    board->display.touch_irq_enabled = false;
    board->display.scanout_on = false;
    board->display.enabled = false;
    board->display.brightness_applied = false;
    board->display.power_phase = BOARD_DISPLAY_POWER_PHASE_PANEL_READY;
    board->display.brightness = BOARD_BRIGHTNESS_DEFAULT;
    board->settings.brightness = BOARD_BRIGHTNESS_DEFAULT;
    return ESP_OK;
}

esp_err_t board_display_deinit(board_context_t *board)
{
    if (board == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    _board_backlight_deinit();
    _board_display_clear(board);
    return ESP_OK;
}

esp_err_t board_display_set_brightness_impl(board_context_t *board,
        uint8_t brightness)
{
    if (board == NULL || !board->display.panel_initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (!board->display.enabled)
    {
        board->display.brightness = brightness;
        board->display.brightness_applied = false;
        return ESP_OK;
    }
    return _board_apply_brightness(board, brightness);
}

esp_err_t board_display_set_power_impl(board_context_t *board, bool on)
{
    if (board == NULL || board->display.port.panel == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = esp_lcd_panel_disp_on_off(board->display.port.panel,
        on);
    if (result == ESP_OK)
    {
        board->display.scanout_on = on;
        board->display.power_phase = on ?
                                     BOARD_DISPLAY_POWER_PHASE_SCANOUT_ON :
                                     BOARD_DISPLAY_POWER_PHASE_PANEL_READY;
    }
    return result;
}

esp_err_t board_display_set_enabled_impl(board_context_t *board, bool on)
{
    if (board == NULL || board->display.port.panel == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    if (on)
    {
        result = esp_lcd_panel_disp_on_off(board->display.port.panel, true);
        if (result == ESP_OK)
        {
            board->display.scanout_on = true;
            result = _board_apply_brightness(board,
                                             board->settings.brightness);
        }
        if (result == ESP_OK)
        {
            result = gpio_intr_enable(BOARD_TOUCH_PIN_INT);
        }
        if (result == ESP_OK)
        {
            board->display.touch_irq_enabled = true;
            board->display.enabled = true;
            board->display.power_phase = BOARD_DISPLAY_POWER_PHASE_ENABLED;
        }
        return result;
    }

    const esp_err_t touch_result = gpio_intr_disable(BOARD_TOUCH_PIN_INT);
    const esp_err_t off_result =
        esp_lcd_panel_disp_on_off(board->display.port.panel, false);
    const esp_err_t brightness_result = _board_apply_brightness(board, 0U);

    board->display.scanout_on = false;
    board->display.enabled = false;
    board->display.touch_irq_enabled = touch_result != ESP_OK;
    board->display.brightness_applied =
        off_result != ESP_OK || brightness_result != ESP_OK;
    if (touch_result == ESP_OK && off_result == ESP_OK &&
            brightness_result == ESP_OK)
    {
        board->display.power_phase = BOARD_DISPLAY_POWER_PHASE_OFF;
    }

    if (touch_result != ESP_OK)
    {
        return touch_result;
    }
    if (off_result != ESP_OK)
    {
        return off_result;
    }
    return brightness_result;
}

esp_err_t board_display_suspend_impl(board_context_t *board)
{
    return board_display_set_enabled_impl(board, false);
}

esp_err_t board_display_resume_prepare_impl(board_context_t *board)
{
    if (board == NULL || !board->display.panel_initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }
    board->display.power_phase = BOARD_DISPLAY_POWER_PHASE_PANEL_READY;
    return ESP_OK;
}

esp_err_t board_display_resume_commit_impl(board_context_t *board)
{
    return board_display_set_enabled_impl(board, true);
}

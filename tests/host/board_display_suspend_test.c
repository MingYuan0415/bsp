#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "board_i2c_panel_io.h"
#include "board_init.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_ft5x06.h"

#define FT5X06_REG_CHIP_ID          (0xA3)
#define FT5X06_REG_POWER_MODE       (0xA5)
#define LCD_CMD_WRCTRLD             (0x53)
#define LCD_CMD_WRDISBV             (0x51)
#define LCD_QSPI_OPCODE_WRITE_CMD   (0x02U)
#define LCD_QSPI_CMD(cmd)           ((((cmd) & 0xFF) << 8) | \
                                      (LCD_QSPI_OPCODE_WRITE_CMD << 24))
#define TEST_HANDLE(value)          ((void *)(uintptr_t)(value))
#define LCD_COMMAND_CAPACITY        (16U)

#if defined(CONFIG_BSP_DISPLAY_TE_SYNC) && CONFIG_BSP_DISPLAY_TE_SYNC
    #define TEST_TE_SYNC_ENABLED        (true)
#else
    #define TEST_TE_SYNC_ENABLED        (false)
#endif

typedef struct display_mock_state
{
    esp_err_t hibernate_result;
    esp_err_t gpio_disable_result;
    esp_err_t gpio_enable_result;
    esp_err_t panel_on_result;
    esp_err_t panel_off_result;
    esp_err_t lcd_tx_failure_result;
    esp_err_t touch_reset_failure_result;
    unsigned int lcd_tx_fail_on_call;
    unsigned int touch_reset_failures_remaining;
    unsigned int gpio_disable_calls;
    unsigned int gpio_enable_calls;
    unsigned int hibernate_calls;
    unsigned int replay_write_calls;
    unsigned int replay_read_calls;
    unsigned int panel_on_calls;
    unsigned int panel_off_calls;
    unsigned int lcd_tx_calls_at_panel_off;
    unsigned int panel_reset_calls;
    unsigned int panel_init_calls;
    unsigned int lcd_tx_calls;
    int lcd_commands[LCD_COMMAND_CAPACITY];
    uint8_t lcd_values[LCD_COMMAND_CAPACITY];
    unsigned int touch_reset_low_calls;
    unsigned int touch_reset_high_calls;
    unsigned int lcd_reset_low_calls;
    unsigned int lcd_reset_high_calls;
    unsigned int lcd_power_high_calls;
    unsigned int lcd_power_low_calls;
    unsigned int retry_delay_calls;
    unsigned int touch_ready_delay_calls;
    unsigned int ten_ms_delay_calls;
    bool psram_dma_direct;
    uint32_t lcd_pclk_hz;
    int lcd_queue_depth;
    bool init_brightness_zero_seen;
    bool init_brightness_nonzero_seen;
    bool init_te_enabled_seen;
    bool init_te_scanline_seen;
    bool verify_init_order_on_panel_on;
} display_mock_state_t;

static display_mock_state_t s_mock;
static TickType_t s_tick_count;

static void _reset_mock(void)
{
    memset(&s_mock, 0, sizeof(s_mock));
    s_mock.hibernate_result = ESP_OK;
    s_mock.gpio_disable_result = ESP_OK;
    s_mock.gpio_enable_result = ESP_OK;
    s_mock.panel_on_result = ESP_OK;
    s_mock.panel_off_result = ESP_OK;
    s_mock.lcd_tx_failure_result = ESP_FAIL;
    s_mock.touch_reset_failure_result = ESP_ERR_TIMEOUT;
    s_tick_count = 0;
}

static board_context_t _make_active_board(void)
{
    board_context_t board = {0};
    board.io_expander = TEST_HANDLE(1);
    board.display.port.panel = TEST_HANDLE(2);
    board.display.port.panel_io = TEST_HANDLE(3);
    board.display.port.touch_io = TEST_HANDLE(4);
    board.display.power_phase = BOARD_DISPLAY_POWER_PHASE_ENABLED;
    board.display.rail_on = true;
    board.display.reset_released = true;
    board.display.panel_reset = true;
    board.display.panel_initialized = true;
    board.display.scanout_on = true;
    board.display.brightness_applied = true;
    board.display.enabled = true;
    board.display.screen_phase = BOARD_SCREEN_PHASE_ACTIVE;
    board.display.touch_irq_enabled = true;
    board.display.touch_reset_released = true;
    board.display.touch_configured = true;
    return board;
}

static board_context_t _make_hidden_board(void)
{
    board_context_t board = _make_active_board();
    board.display.power_phase = BOARD_DISPLAY_POWER_PHASE_SCANOUT_HIDDEN;
    board.display.brightness_applied = false;
    board.display.enabled = false;
    board.display.screen_phase = BOARD_SCREEN_PHASE_TOUCH_CONFIGURED;
    board.display.touch_irq_enabled = false;
    return board;
}

static board_context_t _make_suspended_board(void)
{
    board_context_t board = {0};
    board.io_expander = TEST_HANDLE(1);
    board.display.port.panel = TEST_HANDLE(2);
    board.display.port.panel_io = TEST_HANDLE(3);
    board.display.port.touch_io = TEST_HANDLE(4);
    board.display.brightness = 0x7AU;
    board.display.power_phase = BOARD_DISPLAY_POWER_PHASE_OFF;
    board.display.screen_phase = BOARD_SCREEN_PHASE_SUSPENDED;
    board.display.touch_hibernated = true;
    board.display.touch_reset_released = true;
    return board;
}

esp_err_t board_init_stage_gate(board_init_stage_t stage)
{
    (void)stage;
    return ESP_OK;
}

esp_err_t spi_bus_initialize(spi_host_device_t host,
                             const spi_bus_config_t *bus_config,
                             int dma_channel)
{
    assert(host == SPI2_HOST);
    assert(bus_config != NULL);
    assert(dma_channel == SPI_DMA_CH_AUTO);
    return ESP_OK;
}

esp_err_t spi_bus_free(spi_host_device_t host)
{
    assert(host == SPI2_HOST);
    return ESP_OK;
}

esp_err_t esp_lcd_new_panel_io_spi(
    spi_host_device_t host,
    const esp_lcd_panel_io_spi_config_t *config,
    esp_lcd_panel_io_handle_t *out_io)
{
    assert(host == SPI2_HOST);
    assert(config != NULL);
    assert(out_io != NULL);
    s_mock.psram_dma_direct = config->flags.psram_dma_direct;
    s_mock.lcd_pclk_hz = config->pclk_hz;
    s_mock.lcd_queue_depth = config->trans_queue_depth;
    *out_io = TEST_HANDLE(3);
    return ESP_OK;
}

esp_err_t esp_lcd_new_panel_sh8601(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_dev_config_t *config,
    esp_lcd_panel_handle_t *out_panel)
{
    assert(io == TEST_HANDLE(3));
    assert(config != NULL);
    assert(config->vendor_config != NULL);
    assert(out_panel != NULL);

    const sh8601_vendor_config_t *vendor_config = config->vendor_config;
    for (size_t index = 0; index < vendor_config->init_cmds_size; ++index)
    {
        const sh8601_lcd_init_cmd_t *entry =
            &vendor_config->init_cmds[index];
        if (entry->cmd == LCD_CMD_WRDISBV)
        {
            assert(entry->data != NULL);
            assert(entry->data_bytes == sizeof(uint8_t));
            if (*(const uint8_t *)entry->data == 0U)
            {
                s_mock.init_brightness_zero_seen = true;
            }
            else
            {
                s_mock.init_brightness_nonzero_seen = true;
            }
        }
        else if (entry->cmd == 0x35U)
        {
            s_mock.init_te_enabled_seen = true;
        }
        else if (entry->cmd == 0x44U)
        {
            s_mock.init_te_scanline_seen = true;
        }
    }
    s_mock.verify_init_order_on_panel_on = true;
    *out_panel = TEST_HANDLE(2);
    return ESP_OK;
}

esp_err_t board_i2c_panel_io_create(i2c_master_bus_handle_t bus,
                                    uint8_t device_address,
                                    uint32_t clock_speed_hz,
                                    esp_lcd_panel_io_handle_t *out_io)
{
    assert(bus == TEST_HANDLE(6));
    assert(device_address == ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS);
    assert(clock_speed_hz == BOARD_I2C_CLK_HZ);
    assert(out_io != NULL);
    *out_io = TEST_HANDLE(4);
    return ESP_OK;
}

esp_err_t esp_lcd_touch_new_i2c_ft5x06(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_touch_config_t *config,
    esp_lcd_touch_handle_t *out_touch)
{
    assert(io == TEST_HANDLE(4));
    assert(config != NULL);
    assert(out_touch != NULL);
    *out_touch = TEST_HANDLE(5);
    return ESP_OK;
}

esp_err_t esp_lcd_touch_del(esp_lcd_touch_handle_t touch)
{
    assert(touch == TEST_HANDLE(5));
    return ESP_OK;
}

esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io)
{
    assert(io == TEST_HANDLE(3) || io == TEST_HANDLE(4));
    return ESP_OK;
}

esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t panel)
{
    assert(panel == TEST_HANDLE(2));
    return ESP_OK;
}

int gpio_intr_disable(gpio_num_t gpio_num)
{
    assert(gpio_num == BOARD_I2C_PIN_INT);
    ++s_mock.gpio_disable_calls;
    return s_mock.gpio_disable_result;
}

int gpio_intr_enable(gpio_num_t gpio_num)
{
    assert(gpio_num == BOARD_I2C_PIN_INT);
    ++s_mock.gpio_enable_calls;
    return s_mock.gpio_enable_result;
}

esp_err_t esp_io_expander_set_level(esp_io_expander_handle_t handle,
                                    uint32_t pin, uint8_t level)
{
    assert(handle == TEST_HANDLE(1));
    if (pin == BOARD_EXIO_PIN_TOUCH_RESET)
    {
        if (level == 0U)
        {
            ++s_mock.touch_reset_low_calls;
            if (s_mock.touch_reset_failures_remaining > 0U)
            {
                --s_mock.touch_reset_failures_remaining;
                return s_mock.touch_reset_failure_result;
            }
        }
        else
        {
            ++s_mock.touch_reset_high_calls;
        }
    }
    else if (pin == BOARD_EXIO_PIN_LCD_RESET && level == 0U)
    {
        ++s_mock.lcd_reset_low_calls;
    }
    else if (pin == BOARD_EXIO_PIN_LCD_RESET)
    {
        ++s_mock.lcd_reset_high_calls;
    }
    else if (pin == BOARD_EXIO_PIN_LCD_PWR_EN && level != 0U)
    {
        ++s_mock.lcd_power_high_calls;
    }
    else if (pin == BOARD_EXIO_PIN_LCD_PWR_EN && level == 0U)
    {
        ++s_mock.lcd_power_low_calls;
    }
    else
    {
        assert(false);
    }
    return ESP_OK;
}

esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io,
                                    int command, const void *param,
                                    size_t param_size)
{
    assert(param != NULL);
    assert(param_size == sizeof(uint8_t));
    if (io == TEST_HANDLE(3))
    {
        assert(s_mock.lcd_tx_calls < LCD_COMMAND_CAPACITY);
        s_mock.lcd_commands[s_mock.lcd_tx_calls] = command;
        s_mock.lcd_values[s_mock.lcd_tx_calls] = *(const uint8_t *)param;
        ++s_mock.lcd_tx_calls;
        if (s_mock.lcd_tx_fail_on_call == s_mock.lcd_tx_calls)
        {
            return s_mock.lcd_tx_failure_result;
        }
        return ESP_OK;
    }

    assert(io == TEST_HANDLE(4));
    if (command == FT5X06_REG_POWER_MODE)
    {
        ++s_mock.hibernate_calls;
        return s_mock.hibernate_result;
    }
    ++s_mock.replay_write_calls;
    return ESP_OK;
}

esp_err_t esp_lcd_panel_io_rx_param(esp_lcd_panel_io_handle_t io,
                                    int command, void *param,
                                    size_t param_size)
{
    assert(io == TEST_HANDLE(4));
    assert(command == FT5X06_REG_CHIP_ID);
    assert(param != NULL);
    assert(param_size == sizeof(uint8_t));
    *(uint8_t *)param = 0x54U;
    ++s_mock.replay_read_calls;
    return ESP_OK;
}

esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t panel, bool on)
{
    assert(panel == TEST_HANDLE(2));
    if (on)
    {
        if (s_mock.verify_init_order_on_panel_on)
        {
            assert(s_mock.panel_init_calls > 0U);
            assert(s_mock.init_brightness_zero_seen);
            assert(!s_mock.init_brightness_nonzero_seen);
            s_mock.verify_init_order_on_panel_on = false;
        }
        ++s_mock.panel_on_calls;
        return s_mock.panel_on_result;
    }
    ++s_mock.panel_off_calls;
    s_mock.lcd_tx_calls_at_panel_off = s_mock.lcd_tx_calls;
    return s_mock.panel_off_result;
}

esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t panel)
{
    assert(panel == TEST_HANDLE(2));
    ++s_mock.panel_reset_calls;
    return ESP_OK;
}

esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t panel)
{
    assert(panel == TEST_HANDLE(2));
    ++s_mock.panel_init_calls;
    return ESP_OK;
}

TickType_t xTaskGetTickCount(void)
{
    return s_tick_count;
}

void vTaskDelay(TickType_t ticks)
{
    s_tick_count += ticks;
    if (ticks == pdMS_TO_TICKS(2U))
    {
        ++s_mock.retry_delay_calls;
    }
    if (ticks == pdMS_TO_TICKS(300U))
    {
        ++s_mock.touch_ready_delay_calls;
    }
    if (ticks == pdMS_TO_TICKS(10U))
    {
        ++s_mock.ten_ms_delay_calls;
    }
}

static void _assert_reset_fallback_committed(const board_context_t *board)
{
    assert(_board_display_is_suspend_committed(&board->display));
    assert(!board->display.touch_hibernated);
    assert(!board->display.touch_reset_released);
    assert(!board->display.touch_configured);
    assert(!board->display.touch_irq_enabled);
    assert(s_mock.hibernate_calls == 1U);
    assert(s_mock.panel_off_calls == 1U);
    assert(s_mock.lcd_reset_low_calls == 1U);
    assert(s_mock.lcd_power_low_calls == 1U);
    assert(s_mock.gpio_disable_calls == 1U);
    assert(s_mock.gpio_enable_calls == 0U);
}

static void _test_hibernate_nack_uses_reset_and_powers_off(void)
{
    _reset_mock();
    s_mock.hibernate_result = ESP_ERR_INVALID_RESPONSE;
    board_context_t board = _make_active_board();

    assert(board_display_suspend_impl(&board) == ESP_OK);
    assert(s_mock.touch_reset_low_calls == 1U);
    _assert_reset_fallback_committed(&board);
}

static void _test_touch_reset_transient_failure_is_retried(void)
{
    _reset_mock();
    s_mock.hibernate_result = ESP_FAIL;
    s_mock.touch_reset_failures_remaining = 1U;
    board_context_t board = _make_active_board();

    assert(board_display_suspend_impl(&board) == ESP_OK);
    assert(s_mock.touch_reset_low_calls == 2U);
    assert(s_mock.retry_delay_calls == 1U);
    _assert_reset_fallback_committed(&board);
}

static void _test_touch_reset_failure_restores_irq_and_stops(void)
{
    _reset_mock();
    s_mock.hibernate_result = ESP_FAIL;
    s_mock.touch_reset_failures_remaining = 3U;
    board_context_t board = _make_active_board();

    assert(board_display_suspend_impl(&board) == ESP_ERR_TIMEOUT);
    assert(s_mock.touch_reset_low_calls == 3U);
    assert(s_mock.retry_delay_calls == 2U);
    assert(s_mock.gpio_disable_calls == 1U);
    assert(s_mock.gpio_enable_calls == 1U);
    assert(s_mock.panel_off_calls == 0U);
    assert(board.display.screen_phase == BOARD_SCREEN_PHASE_ACTIVE);
    assert(board.display.touch_irq_enabled);
    assert(board.display.touch_reset_released);
    assert(board.display.touch_configured);
    assert(!board.display.touch_hibernated);
}

static void _test_normal_hibernate_does_not_assert_touch_reset(void)
{
    _reset_mock();
    board_context_t board = _make_active_board();

    assert(board_display_suspend_impl(&board) == ESP_OK);
    assert(_board_display_is_suspend_committed(&board.display));
    assert(board.display.touch_hibernated);
    assert(board.display.touch_reset_released);
    assert(!board.display.touch_configured);
    assert(s_mock.touch_reset_low_calls == 0U);
    assert(s_mock.hibernate_calls == 1U);
}

static void _test_hidden_scanout_suspend_sends_display_off(void)
{
    _reset_mock();
    board_context_t board = _make_hidden_board();

    assert(board_display_suspend_impl(&board) == ESP_OK);
    assert(_board_display_is_suspend_committed(&board.display));
    assert(s_mock.panel_off_calls == 1U);
    assert(!board.display.scanout_on);
    assert(!board.display.enabled);
}

static void _test_panel_failure_restores_reset_quiesced_touch(void)
{
    _reset_mock();
    s_mock.hibernate_result = ESP_FAIL;
    s_mock.panel_off_result = ESP_FAIL;
    board_context_t board = _make_active_board();

    assert(board_display_suspend_impl(&board) == ESP_FAIL);
    assert(s_mock.touch_reset_low_calls == 2U);
    assert(s_mock.touch_reset_high_calls == 1U);
    assert(s_mock.replay_write_calls == 9U);
    assert(s_mock.replay_read_calls == 1U);
    assert(s_mock.touch_ready_delay_calls == 1U);
    assert(s_mock.gpio_enable_calls == 1U);
    assert(board.display.screen_phase == BOARD_SCREEN_PHASE_ACTIVE);
    assert(board.display.touch_irq_enabled);
    assert(board.display.touch_reset_released);
    assert(board.display.touch_configured);
    assert(!board.display.touch_hibernated);
}

static void _test_panel_failure_hard_resets_hibernated_touch(void)
{
    _reset_mock();
    s_mock.panel_off_result = ESP_FAIL;
    board_context_t board = _make_active_board();

    assert(board_display_suspend_impl(&board) == ESP_FAIL);
    assert(s_mock.hibernate_calls == 1U);
    assert(s_mock.touch_reset_low_calls == 1U);
    assert(s_mock.touch_reset_high_calls == 1U);
    assert(s_mock.replay_write_calls == 9U);
    assert(s_mock.replay_read_calls == 1U);
    assert(s_mock.touch_ready_delay_calls == 1U);
    assert(s_mock.gpio_enable_calls == 1U);
    assert(board.display.screen_phase == BOARD_SCREEN_PHASE_ACTIVE);
    assert(board.display.touch_irq_enabled);
    assert(board.display.touch_reset_released);
    assert(board.display.touch_configured);
    assert(!board.display.touch_hibernated);
}

static void _test_init_configures_dma_by_display_mode(void)
{
    _reset_mock();
    board_context_t board =
    {
        .i2c_bus = TEST_HANDLE(6),
        .io_expander = TEST_HANDLE(1),
        .settings = { .brightness = 0xA5U, },
    };

    assert(board_display_init(&board) == ESP_OK);
    assert(s_mock.psram_dma_direct == TEST_TE_SYNC_ENABLED);
    assert(s_mock.lcd_pclk_hz == 60U * 1000U * 1000U);
    assert(s_mock.lcd_queue_depth == 8);
    assert(board.display.port.te.enabled == TEST_TE_SYNC_ENABLED);
    assert(s_mock.init_brightness_zero_seen);
    assert(!s_mock.init_brightness_nonzero_seen);
    assert(s_mock.init_te_enabled_seen);
    assert(s_mock.init_te_scanline_seen);
    assert(s_mock.panel_init_calls == 1U);
    assert(s_mock.panel_on_calls == 1U);
    assert(s_mock.gpio_disable_calls == 1U);
    assert(s_mock.ten_ms_delay_calls == 1U);
    assert(board.display.scanout_on);
    assert(!board.display.enabled);
    assert(!board.display.brightness_applied);
    assert(board.display.brightness == 0xA5U);
    assert(board.display.power_phase ==
           BOARD_DISPLAY_POWER_PHASE_SCANOUT_HIDDEN);
    assert(board.display.screen_phase == BOARD_SCREEN_PHASE_TOUCH_CONFIGURED);
    assert(s_mock.lcd_tx_calls == 0U);

    assert(board_display_deinit(&board) == ESP_OK);
    assert(s_mock.panel_off_calls == 1U);
}

static void _test_hidden_brightness_is_cached_without_panel_write(void)
{
    _reset_mock();
    board_context_t board = _make_hidden_board();

    assert(board_display_set_brightness_impl(&board, 0x62U) == ESP_OK);
    assert(board.display.brightness == 0x62U);
    assert(!board.display.brightness_applied);
    assert(!board.display.enabled);
    assert(board.display.scanout_on);
    assert(board.display.power_phase ==
           BOARD_DISPLAY_POWER_PHASE_SCANOUT_HIDDEN);
    assert(s_mock.lcd_tx_calls == 0U);
}

static void _test_resume_prepare_starts_hidden_scanout_then_commits(void)
{
    _reset_mock();
    board_context_t board = _make_suspended_board();

    assert(board_display_resume_prepare_impl(&board) == ESP_OK);
    assert(board.display.scanout_on);
    assert(!board.display.enabled);
    assert(!board.display.brightness_applied);
    assert(!board.display.touch_irq_enabled);
    assert(board.display.power_phase ==
           BOARD_DISPLAY_POWER_PHASE_SCANOUT_HIDDEN);
    assert(board.display.screen_phase == BOARD_SCREEN_PHASE_TOUCH_CONFIGURED);
    assert(s_mock.panel_on_calls == 1U);
    assert(s_mock.ten_ms_delay_calls == 3U);
    assert(s_mock.lcd_tx_calls == 0U);

    assert(board_display_resume_prepare_impl(&board) == ESP_OK);
    assert(s_mock.panel_on_calls == 1U);

    assert(board_display_resume_commit_impl(&board) == ESP_OK);
    assert(s_mock.lcd_tx_calls == 2U);
    assert(s_mock.lcd_commands[0] == LCD_QSPI_CMD(LCD_CMD_WRCTRLD));
    assert(s_mock.lcd_values[0] == 0x20U);
    assert(s_mock.lcd_commands[1] == LCD_QSPI_CMD(LCD_CMD_WRDISBV));
    assert(s_mock.lcd_values[1] == 0x7AU);
    assert(s_mock.gpio_enable_calls == 1U);
    assert(board.display.scanout_on);
    assert(board.display.brightness_applied);
    assert(board.display.enabled);
    assert(board.display.touch_irq_enabled);
    assert(board.display.power_phase == BOARD_DISPLAY_POWER_PHASE_ENABLED);
    assert(board.display.screen_phase == BOARD_SCREEN_PHASE_ACTIVE);
}

static void _test_brightness_commit_failure_rehides_and_can_suspend(void)
{
    _reset_mock();
    board_context_t board = _make_suspended_board();
    assert(board_display_resume_prepare_impl(&board) == ESP_OK);
    s_mock.lcd_tx_fail_on_call = 2U;

    assert(board_display_resume_commit_impl(&board) == ESP_FAIL);
    assert(s_mock.lcd_tx_calls == 4U);
    assert(s_mock.lcd_values[1] == 0x7AU);
    assert(s_mock.lcd_values[3] == 0U);
    assert(s_mock.gpio_enable_calls == 0U);
    assert(s_mock.panel_off_calls == 1U);
    assert(s_mock.lcd_tx_calls_at_panel_off == 4U);
    assert(!board.display.scanout_on);
    assert(!board.display.enabled);
    assert(!board.display.brightness_applied);
    assert(!board.display.touch_irq_enabled);
    assert(board.display.power_phase ==
           BOARD_DISPLAY_POWER_PHASE_PANEL_INITIALIZED);

    s_mock.lcd_tx_fail_on_call = 0U;
    assert(board_display_suspend_impl(&board) == ESP_OK);
    assert(_board_display_is_suspend_committed(&board.display));
    assert(s_mock.panel_off_calls == 1U);
    assert(s_mock.lcd_reset_low_calls == 2U);
    assert(s_mock.lcd_power_low_calls == 1U);
}

static void _test_irq_commit_failure_rehides_and_can_suspend(void)
{
    _reset_mock();
    board_context_t board = _make_suspended_board();
    assert(board_display_resume_prepare_impl(&board) == ESP_OK);
    s_mock.gpio_enable_result = ESP_ERR_TIMEOUT;

    assert(board_display_resume_commit_impl(&board) == ESP_ERR_TIMEOUT);
    assert(s_mock.lcd_tx_calls == 4U);
    assert(s_mock.lcd_values[1] == 0x7AU);
    assert(s_mock.lcd_values[3] == 0U);
    assert(s_mock.gpio_enable_calls == 1U);
    assert(s_mock.gpio_disable_calls == 1U);
    assert(s_mock.panel_off_calls == 1U);
    assert(s_mock.lcd_tx_calls_at_panel_off == 4U);
    assert(!board.display.scanout_on);
    assert(!board.display.enabled);
    assert(!board.display.brightness_applied);
    assert(!board.display.touch_irq_enabled);
    assert(board.display.power_phase ==
           BOARD_DISPLAY_POWER_PHASE_PANEL_INITIALIZED);

    s_mock.gpio_enable_result = ESP_OK;
    assert(board_display_suspend_impl(&board) == ESP_OK);
    assert(_board_display_is_suspend_committed(&board.display));
    assert(s_mock.panel_off_calls == 1U);
    assert(s_mock.lcd_reset_low_calls == 2U);
    assert(s_mock.lcd_power_low_calls == 1U);
}

int main(void)
{
    _test_hibernate_nack_uses_reset_and_powers_off();
    _test_touch_reset_transient_failure_is_retried();
    _test_touch_reset_failure_restores_irq_and_stops();
    _test_normal_hibernate_does_not_assert_touch_reset();
    _test_hidden_scanout_suspend_sends_display_off();
    _test_panel_failure_restores_reset_quiesced_touch();
    _test_panel_failure_hard_resets_hibernated_touch();
    _test_init_configures_dma_by_display_mode();
    _test_hidden_brightness_is_cached_without_panel_write();
    _test_resume_prepare_starts_hidden_scanout_then_commits();
    _test_brightness_commit_failure_rehides_and_can_suspend();
    _test_irq_commit_failure_rehides_and_can_suspend();
    return 0;
}

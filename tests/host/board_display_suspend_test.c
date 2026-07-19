#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "board_init.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#define FT5X06_REG_CHIP_ID          (0xA3)
#define FT5X06_REG_POWER_MODE       (0xA5)
#define TEST_HANDLE(value)          ((void *)(uintptr_t)(value))

typedef struct display_mock_state
{
    esp_err_t hibernate_result;
    esp_err_t panel_off_result;
    esp_err_t touch_reset_failure_result;
    unsigned int touch_reset_failures_remaining;
    unsigned int gpio_disable_calls;
    unsigned int gpio_enable_calls;
    unsigned int hibernate_calls;
    unsigned int replay_write_calls;
    unsigned int replay_read_calls;
    unsigned int panel_off_calls;
    unsigned int touch_reset_low_calls;
    unsigned int touch_reset_high_calls;
    unsigned int lcd_reset_low_calls;
    unsigned int lcd_power_low_calls;
    unsigned int retry_delay_calls;
    unsigned int touch_ready_delay_calls;
} display_mock_state_t;

static display_mock_state_t s_mock;
static TickType_t s_tick_count;

static void _reset_mock(void)
{
    memset(&s_mock, 0, sizeof(s_mock));
    s_mock.hibernate_result = ESP_OK;
    s_mock.panel_off_result = ESP_OK;
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
    board.display.brightness_applied = true;
    board.display.enabled = true;
    board.display.screen_phase = BOARD_SCREEN_PHASE_ACTIVE;
    board.display.touch_irq_enabled = true;
    board.display.touch_reset_released = true;
    board.display.touch_configured = true;
    return board;
}

int gpio_intr_disable(gpio_num_t gpio_num)
{
    assert(gpio_num == BOARD_I2C_PIN_INT);
    ++s_mock.gpio_disable_calls;
    return ESP_OK;
}

int gpio_intr_enable(gpio_num_t gpio_num)
{
    assert(gpio_num == BOARD_I2C_PIN_INT);
    ++s_mock.gpio_enable_calls;
    return ESP_OK;
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
    assert(io == TEST_HANDLE(4));
    assert(param != NULL);
    assert(param_size == sizeof(uint8_t));
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
    assert(!on);
    ++s_mock.panel_off_calls;
    return s_mock.panel_off_result;
}

esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t panel)
{
    assert(panel == TEST_HANDLE(2));
    return ESP_OK;
}

esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t panel)
{
    assert(panel == TEST_HANDLE(2));
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

int main(void)
{
    _test_hibernate_nack_uses_reset_and_powers_off();
    _test_touch_reset_transient_failure_is_retried();
    _test_touch_reset_failure_restores_irq_and_stops();
    _test_normal_hibernate_does_not_assert_touch_reset();
    _test_panel_failure_restores_reset_quiesced_touch();
    _test_panel_failure_hard_resets_hibernated_touch();
    return 0;
}

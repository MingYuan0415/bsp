#include <assert.h>
#include <stdbool.h>

#include "board_init.h"

static board_display_state_t _make_committed_state(void)
{
    const board_display_state_t display =
    {
        .power_phase = BOARD_DISPLAY_POWER_PHASE_OFF,
        .screen_phase = BOARD_SCREEN_PHASE_SUSPENDED,
        .touch_hibernated = true,
        .touch_reset_released = true,
    };
    return display;
}

static void _test_touch_quiescent_paths(void)
{
    board_display_state_t display = _make_committed_state();
    assert(_board_display_is_suspend_committed(&display));

    display.touch_hibernated = false;
    display.touch_reset_released = false;
    assert(_board_display_is_suspend_committed(&display));

    display.touch_reset_released = true;
    assert(!_board_display_is_suspend_committed(&display));
}

static void _test_complete_off_state_required(void)
{
    board_display_state_t display = _make_committed_state();

    display.screen_phase = BOARD_SCREEN_PHASE_SUSPENDING;
    assert(!_board_display_is_suspend_committed(&display));
    display.screen_phase = BOARD_SCREEN_PHASE_SUSPENDED;

    display.power_phase = BOARD_DISPLAY_POWER_PHASE_RESET_ASSERTED;
    assert(!_board_display_is_suspend_committed(&display));
    display.power_phase = BOARD_DISPLAY_POWER_PHASE_OFF;

    display.rail_on = true;
    assert(!_board_display_is_suspend_committed(&display));
    display.rail_on = false;

    display.scanout_on = true;
    assert(!_board_display_is_suspend_committed(&display));
    display.scanout_on = false;

    display.enabled = true;
    assert(!_board_display_is_suspend_committed(&display));
    display.enabled = false;

    display.touch_irq_enabled = true;
    assert(!_board_display_is_suspend_committed(&display));
}

static void _test_io_expander_waits_for_dependents(void)
{
    board_context_t board = {0};
    assert(_board_io_expander_dependents_released(&board, false));

    board.sd_card = (board_sdspi_t *)(uintptr_t)1U;
    assert(!_board_io_expander_dependents_released(&board, false));

    board.sd_card = NULL;
    board.display.port.panel = (esp_lcd_panel_handle_t)(uintptr_t)1U;
    assert(!_board_io_expander_dependents_released(&board, false));

    board.display.port.panel = NULL;
    board.imu = (board_imu_t *)(uintptr_t)1U;
    assert(!_board_io_expander_dependents_released(&board, false));

    board.imu = NULL;
    assert(!_board_io_expander_dependents_released(&board, true));
}

static void _test_i2c_waits_for_every_device(void)
{
    board_context_t board = {0};
    assert(_board_i2c_dependents_released(&board, false, false));

    board.io_expander_device = (board_tca9554_t *)(uintptr_t)1U;
    assert(!_board_i2c_dependents_released(&board, false, false));
    board.io_expander_device = NULL;

    assert(!_board_i2c_dependents_released(&board, true, false));
    assert(!_board_i2c_dependents_released(&board, false, true));

    board.audio_initialized = true;
    assert(!_board_i2c_dependents_released(&board, false, false));
    board.audio_initialized = false;

    board.imu = (board_imu_t *)(uintptr_t)1U;
    assert(!_board_i2c_dependents_released(&board, false, false));
    board.imu = NULL;

    board.sd_card = (board_sdspi_t *)(uintptr_t)1U;
    assert(!_board_i2c_dependents_released(&board, false, false));
    board.sd_card = NULL;

    board.display.port.touch = (esp_lcd_touch_handle_t)(uintptr_t)1U;
    assert(!_board_i2c_dependents_released(&board, false, false));
}

int main(void)
{
    _test_touch_quiescent_paths();
    _test_complete_off_state_required();
    _test_io_expander_waits_for_dependents();
    _test_i2c_waits_for_every_device();
    return 0;
}

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

    display.enabled = true;
    assert(!_board_display_is_suspend_committed(&display));
    display.enabled = false;

    display.touch_irq_enabled = true;
    assert(!_board_display_is_suspend_committed(&display));
}

int main(void)
{
    _test_touch_quiescent_paths();
    _test_complete_off_state_required();
    return 0;
}

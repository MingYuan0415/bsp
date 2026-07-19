#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "board_audio.h"
#include "board_audio_fakes.h"

static int s_i2c_bus;

int main(void)
{
    board_audio_fakes_reset();
    board_audio_fake_state_t *fake = board_audio_fakes_state();

    assert(board_audio_init(&s_i2c_bus) == ESP_OK);
    assert(bsp_audio_set_pa(true) == ESP_OK);
    assert(bsp_audio_start() == ESP_OK);
    assert(bsp_audio_is_started());

    uint8_t samples[16] = {0};
    size_t received = 0U;
    assert(bsp_audio_read(samples, sizeof(samples), &received, 20U) == ESP_OK);
    assert(received == sizeof(samples));
    assert(fake->gpio_set_level_calls == 0U);

    assert(bsp_audio_stop() == ESP_OK);
    assert(!bsp_audio_is_started());
    assert(fake->i2s_delete_calls == 2U);
    assert(fake->codec_delete_calls == 1U);
    assert(board_audio_deinit() == ESP_OK);

    puts("board audio no-PA lifecycle regression passed");
    return 0;
}

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "board_audio.h"
#include "board_audio_fakes.h"
#include "es8311_codec.h"

static int s_i2c_bus;

static void _assert_i2s_format(const board_audio_fake_state_t *fake,
                               uint32_t sample_rate_hz,
                               uint8_t bits_per_sample, uint8_t channels,
                               uint16_t mclk_multiple)
{
    for (unsigned index = 0U; index < BOARD_AUDIO_FAKE_CHANNEL_COUNT; ++index)
    {
        assert(fake->i2s_config[index].sample_rate_hz == sample_rate_hz);
        assert(fake->i2s_config[index].bits_per_sample == bits_per_sample);
        assert(fake->i2s_config[index].channels == channels);
        assert(fake->i2s_config[index].mclk_multiple == mclk_multiple);
    }
}

static void _test_partial_mutex_failure(void)
{
    board_audio_fakes_reset();
    board_audio_fake_state_t *fake = board_audio_fakes_state();
    fake->fail_mutex_create_call = 2U;

    assert(board_audio_init(&s_i2c_bus) == ESP_ERR_NO_MEM);
    assert(fake->mutex_create_calls == 2U);
    assert(fake->mutex_delete_calls == 1U);
    assert(board_audio_deinit() == ESP_OK);
}

static void _test_i2s_init_rollback(void)
{
    board_audio_fakes_reset();
    board_audio_fake_state_t *fake = board_audio_fakes_state();
    fake->fail_i2s_init_call = 2U;

    assert(board_audio_init(&s_i2c_bus) == ESP_FAIL);
    assert(fake->i2s_delete_calls == 2U);
    assert(fake->mutex_delete_calls == 3U);
    assert(board_audio_deinit() == ESP_OK);
}

static void _test_format_and_lifecycle(void)
{
    board_audio_fakes_reset();
    board_audio_fake_state_t *fake = board_audio_fakes_state();

    assert(board_audio_init(&s_i2c_bus) == ESP_OK);
    assert(bsp_audio_is_available());
    assert(fake->codec_ctrl_address == ES8311_CODEC_DEFAULT_ADDR);
    assert(fake->codec_mclk_multiple == 384U);
    assert(fake->codec_pa_voltage == 3.3F);
    assert(fake->codec_dac_voltage == 3.3F);
    assert(fake->codec_pa_gain_db == 6.02F);
    _assert_i2s_format(fake, 16000U, 16U, 2U, 384U);

    const bsp_audio_config_t mismatched_24_bit =
    {
        .sample_rate_hz = 44100U,
        .bits_per_sample = 24U,
        .channels = 2U,
        .mclk_multiple = 256U,
    };
    assert(bsp_audio_configure(&mismatched_24_bit) == ESP_ERR_INVALID_ARG);

    const bsp_audio_config_t format_24_bit =
    {
        .sample_rate_hz = 48000U,
        .bits_per_sample = 24U,
        .channels = 2U,
        .mclk_multiple = 384U,
    };
    assert(bsp_audio_configure(&format_24_bit) == ESP_OK);
    assert(fake->codec_mclk_multiple == 384U);
    _assert_i2s_format(fake, 48000U, 24U, 2U, 384U);
    assert(bsp_audio_start() == ESP_OK);
    assert(fake->opened_format.sample_rate == 48000U);
    assert(fake->opened_format.bits_per_sample == 24U);
    assert(fake->opened_format.mclk_multiple == 384U);
    assert(bsp_audio_stop() == ESP_OK);

    assert(board_audio_deinit() == ESP_OK);
    assert(fake->mutex_delete_calls == 3U);
    assert(fake->i2s_delete_calls == 2U);
    assert(!bsp_audio_is_available());
}

static void _test_i2s_reconfigure_rollback(void)
{
    board_audio_fakes_reset();
    board_audio_fake_state_t *fake = board_audio_fakes_state();
    assert(board_audio_init(&s_i2c_bus) == ESP_OK);

    const bsp_audio_config_t format_24_bit =
    {
        .sample_rate_hz = 48000U,
        .bits_per_sample = 24U,
        .channels = 2U,
        .mclk_multiple = 384U,
    };
    fake->fail_i2s_clock_reconfig_call = 2U;
    assert(bsp_audio_configure(&format_24_bit) == ESP_FAIL);
    assert(bsp_audio_is_available());
    _assert_i2s_format(fake, 16000U, 16U, 2U, 384U);
    assert(fake->i2s_clock_reconfig_calls == 4U);
    assert(fake->i2s_slot_reconfig_calls == 3U);
    assert(fake->codec_create_calls == 1U);

    assert(bsp_audio_start() == ESP_OK);
    assert(fake->opened_format.sample_rate == 16000U);
    assert(fake->opened_format.bits_per_sample == 16U);
    assert(fake->opened_format.mclk_multiple == 384U);
    assert(bsp_audio_stop() == ESP_OK);
    assert(board_audio_deinit() == ESP_OK);
}

static void _test_effective_default_mclk(void)
{
    board_audio_fakes_reset();
    board_audio_fake_state_t *fake = board_audio_fakes_state();
    assert(board_audio_init(&s_i2c_bus) == ESP_OK);

    const bsp_audio_config_t default_mclk =
    {
        .sample_rate_hz = 44100U,
        .bits_per_sample = 16U,
        .channels = 2U,
        .mclk_multiple = 0U,
    };
    assert(bsp_audio_configure(&default_mclk) == ESP_OK);
    assert(fake->codec_mclk_multiple == 256U);
    _assert_i2s_format(fake, 44100U, 16U, 2U, 256U);

    assert(bsp_audio_start() == ESP_OK);
    assert(fake->opened_format.mclk_multiple == 256U);
    assert(bsp_audio_stop() == ESP_OK);
    assert(board_audio_deinit() == ESP_OK);
}

static void _test_stop_pa_retry(void)
{
    board_audio_fakes_reset();
    board_audio_fake_state_t *fake = board_audio_fakes_state();
    assert(board_audio_init(&s_i2c_bus) == ESP_OK);
    assert(bsp_audio_start() == ESP_OK);
    assert(fake->gpio_low_calls == 1U);
    assert(fake->gpio_high_calls == 1U);

    fake->gpio_low_failures_remaining = 1U;
    assert(bsp_audio_stop() == ESP_FAIL);
    assert(!bsp_audio_is_started());
    assert(fake->codec_close_calls == 1U);
    assert(fake->gpio_low_calls == 2U);

    assert(bsp_audio_stop() == ESP_OK);
    assert(fake->codec_close_calls == 1U);
    assert(fake->gpio_low_calls == 3U);
    assert(board_audio_deinit() == ESP_OK);
}

static void _test_partial_codec_open_cleanup_retry(void)
{
    board_audio_fakes_reset();
    board_audio_fake_state_t *fake = board_audio_fakes_state();
    assert(board_audio_init(&s_i2c_bus) == ESP_OK);

    fake->codec_open_failures_remaining = 1U;
    fake->codec_close_failures_remaining = 1U;
    assert(bsp_audio_start() == ESP_FAIL);
    assert(!bsp_audio_is_started());
    assert(fake->codec_opened);
    assert(fake->codec_open_calls == 1U);
    assert(fake->codec_close_calls == 1U);

    const bsp_audio_config_t alternate =
    {
        .sample_rate_hz = 48000U,
        .bits_per_sample = 16U,
        .channels = 2U,
        .mclk_multiple = 384U,
    };
    assert(bsp_audio_configure(&alternate) == ESP_ERR_INVALID_STATE);

    assert(bsp_audio_start() == ESP_OK);
    assert(bsp_audio_is_started());
    assert(fake->codec_open_calls == 2U);
    assert(fake->codec_close_calls == 2U);
    assert(bsp_audio_stop() == ESP_OK);
    assert(!fake->codec_opened);
    assert(board_audio_deinit() == ESP_OK);
}

static void _test_deinit_pa_retry(void)
{
    board_audio_fakes_reset();
    board_audio_fake_state_t *fake = board_audio_fakes_state();
    assert(board_audio_init(&s_i2c_bus) == ESP_OK);
    assert(bsp_audio_start() == ESP_OK);

    fake->gpio_low_failures_remaining = 1U;
    assert(board_audio_deinit() == ESP_FAIL);
    assert(!bsp_audio_is_available());
    assert(fake->mutex_delete_calls == 0U);
    assert(fake->codec_delete_calls == 1U);
    assert(fake->gpio_low_calls == 2U);
    const unsigned codec_close_calls = fake->codec_close_calls;

    assert(board_audio_deinit() == ESP_OK);
    assert(fake->codec_close_calls == codec_close_calls);
    assert(fake->gpio_low_calls == 3U);
    assert(fake->mutex_delete_calls == 3U);
}

static void _test_i2s_deinit_retry(void)
{
    board_audio_fakes_reset();
    board_audio_fake_state_t *fake = board_audio_fakes_state();
    assert(board_audio_init(&s_i2c_bus) == ESP_OK);

    fake->i2s_delete_failures_remaining = 1U;
    assert(board_audio_deinit() == ESP_FAIL);
    assert(fake->mutex_delete_calls == 0U);
    assert(!bsp_audio_is_available());

    assert(board_audio_deinit() == ESP_OK);
    assert(fake->mutex_delete_calls == 3U);
    assert(fake->i2s_delete_calls == 3U);
}

int main(void)
{
    assert(board_audio_deinit() == ESP_OK);
    _test_partial_mutex_failure();
    _test_i2s_init_rollback();
    _test_format_and_lifecycle();
    _test_i2s_reconfigure_rollback();
    _test_effective_default_mclk();
    _test_stop_pa_retry();
    _test_partial_codec_open_cleanup_retry();
    _test_deinit_pa_retry();
    _test_i2s_deinit_retry();
    puts("board audio lifecycle regression passed");
    return 0;
}

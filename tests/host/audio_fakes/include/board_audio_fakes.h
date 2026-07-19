#ifndef __BOARD_AUDIO_FAKES_H__
#define __BOARD_AUDIO_FAKES_H__

#include <stdint.h>

#include "esp_codec_dev.h"

#define BOARD_AUDIO_FAKE_TX_CHANNEL (0U)
#define BOARD_AUDIO_FAKE_RX_CHANNEL (1U)
#define BOARD_AUDIO_FAKE_CHANNEL_COUNT (2U)

typedef struct board_audio_fake_i2s_config
{
    uint32_t sample_rate_hz;
    uint16_t mclk_multiple;
    uint8_t bits_per_sample;
    uint8_t channels;
} board_audio_fake_i2s_config_t;

typedef struct board_audio_fake_state
{
    unsigned mutex_create_calls;
    unsigned mutex_delete_calls;
    unsigned fail_mutex_create_call;
    unsigned gpio_set_level_calls;
    unsigned gpio_low_calls;
    unsigned gpio_high_calls;
    unsigned gpio_low_failures_remaining;
    unsigned i2s_init_calls;
    unsigned fail_i2s_init_call;
    unsigned i2s_clock_reconfig_calls;
    unsigned fail_i2s_clock_reconfig_call;
    unsigned i2s_slot_reconfig_calls;
    unsigned fail_i2s_slot_reconfig_call;
    unsigned i2s_delete_calls;
    unsigned i2s_delete_failures_remaining;
    unsigned codec_create_calls;
    unsigned codec_delete_calls;
    unsigned codec_open_calls;
    unsigned codec_close_calls;
    unsigned codec_open_failures_remaining;
    unsigned codec_close_failures_remaining;
    bool codec_opened;
    uint8_t codec_ctrl_address;
    uint16_t codec_mclk_multiple;
    float codec_pa_voltage;
    float codec_dac_voltage;
    float codec_pa_gain_db;
    esp_codec_dev_sample_info_t opened_format;
    board_audio_fake_i2s_config_t i2s_config[BOARD_AUDIO_FAKE_CHANNEL_COUNT];
} board_audio_fake_state_t;

void board_audio_fakes_reset(void);
board_audio_fake_state_t *board_audio_fakes_state(void);

#endif /* __BOARD_AUDIO_FAKES_H__ */

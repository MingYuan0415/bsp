#ifndef __AUDIO_FAKE_ES8311_CODEC_H__
#define __AUDIO_FAKE_ES8311_CODEC_H__

#include <stdbool.h>
#include <stdint.h>

#include "esp_codec_dev_defaults.h"

#define ES8311_CODEC_DEFAULT_ADDR 0x30
#define ESP_CODEC_DEV_WORK_MODE_BOTH 3

typedef struct es8311_codec_cfg
{
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    int codec_mode;
    int16_t pa_pin;
    bool pa_reverted;
    bool master_mode;
    bool use_mclk;
    bool digital_mic;
    bool invert_mclk;
    bool invert_sclk;
    struct
    {
        float pa_voltage;
        float codec_dac_voltage;
        float pa_gain;
    } hw_gain;
    bool no_dac_ref;
    uint16_t mclk_div;
} es8311_codec_cfg_t;

const audio_codec_if_t *es8311_codec_new(es8311_codec_cfg_t *config);

#endif /* __AUDIO_FAKE_ES8311_CODEC_H__ */

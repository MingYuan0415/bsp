#ifndef __AUDIO_FAKE_CODEC_DEFAULTS_H__
#define __AUDIO_FAKE_CODEC_DEFAULTS_H__

#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_codec_dev.h"

typedef struct audio_codec_ctrl_if_t audio_codec_ctrl_if_t;
typedef struct audio_codec_gpio_if_t audio_codec_gpio_if_t;

typedef struct audio_codec_i2c_cfg
{
    uint8_t port;
    uint8_t addr;
    void *bus_handle;
} audio_codec_i2c_cfg_t;

typedef struct audio_codec_i2s_cfg
{
    uint8_t port;
    i2s_chan_handle_t rx_handle;
    i2s_chan_handle_t tx_handle;
    int clk_src;
} audio_codec_i2s_cfg_t;

const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(
    audio_codec_i2c_cfg_t *config);
const audio_codec_gpio_if_t *audio_codec_new_gpio(void);
const audio_codec_data_if_t *audio_codec_new_i2s_data(
    audio_codec_i2s_cfg_t *config);
int audio_codec_delete_codec_if(const audio_codec_if_t *interface);
int audio_codec_delete_ctrl_if(const audio_codec_ctrl_if_t *interface);
int audio_codec_delete_gpio_if(const audio_codec_gpio_if_t *interface);
int audio_codec_delete_data_if(const audio_codec_data_if_t *interface);

#endif /* __AUDIO_FAKE_CODEC_DEFAULTS_H__ */

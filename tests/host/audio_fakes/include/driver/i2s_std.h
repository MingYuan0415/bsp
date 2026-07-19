#ifndef __AUDIO_FAKE_I2S_STD_H__
#define __AUDIO_FAKE_I2S_STD_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef void *i2s_chan_handle_t;

typedef struct i2s_chan_config
{
    int unused;
} i2s_chan_config_t;

typedef enum i2s_data_bit_width
{
    I2S_DATA_BIT_WIDTH_16BIT = 16,
    I2S_DATA_BIT_WIDTH_24BIT = 24,
    I2S_DATA_BIT_WIDTH_32BIT = 32,
} i2s_data_bit_width_t;

typedef enum i2s_slot_mode
{
    I2S_SLOT_MODE_MONO = 1,
    I2S_SLOT_MODE_STEREO = 2,
} i2s_slot_mode_t;

typedef enum i2s_mclk_multiple
{
    I2S_MCLK_MULTIPLE_256 = 256,
} i2s_mclk_multiple_t;

typedef struct i2s_std_clk_config
{
    uint32_t sample_rate_hz;
    i2s_mclk_multiple_t mclk_multiple;
} i2s_std_clk_config_t;

typedef struct i2s_std_slot_config
{
    i2s_data_bit_width_t data_bit_width;
    i2s_slot_mode_t slot_mode;
} i2s_std_slot_config_t;

typedef struct i2s_std_gpio_config
{
    int mclk;
    int bclk;
    int ws;
    int dout;
    int din;
    struct
    {
        bool mclk_inv;
        bool bclk_inv;
        bool ws_inv;
    } invert_flags;
} i2s_std_gpio_config_t;

typedef struct i2s_std_config
{
    i2s_std_clk_config_t clk_cfg;
    i2s_std_slot_config_t slot_cfg;
    i2s_std_gpio_config_t gpio_cfg;
} i2s_std_config_t;

#define I2S_ROLE_MASTER 1

#define I2S_CHANNEL_DEFAULT_CONFIG(port, role) \
    ((i2s_chan_config_t){.unused = (port) + (role)})
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) \
    ((i2s_std_clk_config_t){.sample_rate_hz = (rate), \
                            .mclk_multiple = I2S_MCLK_MULTIPLE_256})
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, mode_value) \
    ((i2s_std_slot_config_t){.data_bit_width = (bits), \
                             .slot_mode = (mode_value)})

esp_err_t i2s_new_channel(const i2s_chan_config_t *config,
                          i2s_chan_handle_t *tx_channel,
                          i2s_chan_handle_t *rx_channel);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t channel,
                                    const i2s_std_config_t *config);
esp_err_t i2s_channel_reconfig_std_clock(
    i2s_chan_handle_t channel, const i2s_std_clk_config_t *config);
esp_err_t i2s_channel_reconfig_std_slot(
    i2s_chan_handle_t channel, const i2s_std_slot_config_t *config);
esp_err_t i2s_del_channel(i2s_chan_handle_t channel);
esp_err_t i2s_channel_write(i2s_chan_handle_t channel, const void *data,
                            size_t bytes, size_t *written, TickType_t ticks);
esp_err_t i2s_channel_read(i2s_chan_handle_t channel, void *data,
                           size_t bytes, size_t *read, TickType_t ticks);

#endif /* __AUDIO_FAKE_I2S_STD_H__ */

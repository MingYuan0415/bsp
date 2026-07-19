#include "board_audio_fakes.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "es8311_codec.h"
#include "freertos/semphr.h"

#define BOARD_AUDIO_FAKE_MUTEX_COUNT (3U)

struct audio_fake_semaphore
{
    bool active;
    bool taken;
};

static board_audio_fake_state_t s_state;
static struct audio_fake_semaphore s_mutexes[BOARD_AUDIO_FAKE_MUTEX_COUNT];
static int s_tx_channel;
static int s_rx_channel;
static int s_data_interface;
static int s_ctrl_interface;
static int s_gpio_interface;
static int s_codec_interface;
static int s_codec_device;

static unsigned _channel_index(i2s_chan_handle_t channel)
{
    assert(channel == &s_tx_channel || channel == &s_rx_channel);
    return channel == &s_tx_channel ?
           BOARD_AUDIO_FAKE_TX_CHANNEL : BOARD_AUDIO_FAKE_RX_CHANNEL;
}

static void _record_i2s_config(i2s_chan_handle_t channel,
                               const i2s_std_config_t *config)
{
    board_audio_fake_i2s_config_t *record =
        &s_state.i2s_config[_channel_index(channel)];
    record->sample_rate_hz = config->clk_cfg.sample_rate_hz;
    record->mclk_multiple = (uint16_t)config->clk_cfg.mclk_multiple;
    record->bits_per_sample = (uint8_t)config->slot_cfg.data_bit_width;
    record->channels = (uint8_t)config->slot_cfg.slot_mode;
}

void board_audio_fakes_reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(s_mutexes, 0, sizeof(s_mutexes));
}

board_audio_fake_state_t *board_audio_fakes_state(void)
{
    return &s_state;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    ++s_state.mutex_create_calls;
    if (s_state.fail_mutex_create_call == s_state.mutex_create_calls)
    {
        return NULL;
    }
    const unsigned index = (s_state.mutex_create_calls - 1U) %
                           BOARD_AUDIO_FAKE_MUTEX_COUNT;
    s_mutexes[index].active = true;
    s_mutexes[index].taken = false;
    return &s_mutexes[index];
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks)
{
    assert(semaphore != NULL && semaphore->active && !semaphore->taken);
    assert(ticks == portMAX_DELAY);
    semaphore->taken = true;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    assert(semaphore != NULL && semaphore->active && semaphore->taken);
    semaphore->taken = false;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore)
{
    assert(semaphore != NULL && semaphore->active && !semaphore->taken);
    semaphore->active = false;
    ++s_state.mutex_delete_calls;
}

esp_err_t gpio_config(const gpio_config_t *config)
{
    assert(config != NULL);
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio, uint32_t level)
{
    assert(gpio == 46);
    assert(level <= 1U);
    ++s_state.gpio_set_level_calls;
    if (level == 0U)
    {
        ++s_state.gpio_low_calls;
        if (s_state.gpio_low_failures_remaining > 0U)
        {
            --s_state.gpio_low_failures_remaining;
            return ESP_FAIL;
        }
    }
    else
    {
        ++s_state.gpio_high_calls;
    }
    return ESP_OK;
}

esp_err_t i2s_new_channel(const i2s_chan_config_t *config,
                          i2s_chan_handle_t *tx_channel,
                          i2s_chan_handle_t *rx_channel)
{
    assert(config != NULL && tx_channel != NULL && rx_channel != NULL);
    *tx_channel = &s_tx_channel;
    *rx_channel = &s_rx_channel;
    return ESP_OK;
}

esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t channel,
                                    const i2s_std_config_t *config)
{
    assert((channel == &s_tx_channel || channel == &s_rx_channel) &&
           config != NULL);
    ++s_state.i2s_init_calls;
    if (s_state.fail_i2s_init_call == s_state.i2s_init_calls)
    {
        return ESP_FAIL;
    }
    _record_i2s_config(channel, config);
    return ESP_OK;
}

esp_err_t i2s_channel_reconfig_std_clock(
    i2s_chan_handle_t channel, const i2s_std_clk_config_t *config)
{
    const unsigned index = _channel_index(channel);
    assert(config != NULL);
    ++s_state.i2s_clock_reconfig_calls;
    if (s_state.fail_i2s_clock_reconfig_call ==
            s_state.i2s_clock_reconfig_calls)
    {
        return ESP_FAIL;
    }
    s_state.i2s_config[index].sample_rate_hz = config->sample_rate_hz;
    s_state.i2s_config[index].mclk_multiple =
        (uint16_t)config->mclk_multiple;
    return ESP_OK;
}

esp_err_t i2s_channel_reconfig_std_slot(
    i2s_chan_handle_t channel, const i2s_std_slot_config_t *config)
{
    const unsigned index = _channel_index(channel);
    assert(config != NULL);
    ++s_state.i2s_slot_reconfig_calls;
    if (s_state.fail_i2s_slot_reconfig_call ==
            s_state.i2s_slot_reconfig_calls)
    {
        return ESP_FAIL;
    }
    s_state.i2s_config[index].bits_per_sample =
        (uint8_t)config->data_bit_width;
    s_state.i2s_config[index].channels = (uint8_t)config->slot_mode;
    return ESP_OK;
}

esp_err_t i2s_del_channel(i2s_chan_handle_t channel)
{
    assert(channel == &s_tx_channel || channel == &s_rx_channel);
    ++s_state.i2s_delete_calls;
    if (s_state.i2s_delete_failures_remaining > 0U)
    {
        --s_state.i2s_delete_failures_remaining;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t i2s_channel_write(i2s_chan_handle_t channel, const void *data,
                            size_t bytes, size_t *written, TickType_t ticks)
{
    assert(channel == &s_tx_channel && data != NULL);
    (void)ticks;
    *written = bytes;
    return ESP_OK;
}

esp_err_t i2s_channel_read(i2s_chan_handle_t channel, void *data,
                           size_t bytes, size_t *read, TickType_t ticks)
{
    assert(channel == &s_rx_channel && data != NULL);
    (void)ticks;
    memset(data, 0, bytes);
    *read = bytes;
    return ESP_OK;
}

const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(
    audio_codec_i2c_cfg_t *config)
{
    assert(config != NULL && config->bus_handle != NULL);
    s_state.codec_ctrl_address = config->addr;
    return (const audio_codec_ctrl_if_t *)&s_ctrl_interface;
}

const audio_codec_gpio_if_t *audio_codec_new_gpio(void)
{
    return (const audio_codec_gpio_if_t *)&s_gpio_interface;
}

const audio_codec_data_if_t *audio_codec_new_i2s_data(
    audio_codec_i2s_cfg_t *config)
{
    assert(config != NULL && config->tx_handle == &s_tx_channel &&
           config->rx_handle == &s_rx_channel);
    return (const audio_codec_data_if_t *)&s_data_interface;
}

const audio_codec_if_t *es8311_codec_new(es8311_codec_cfg_t *config)
{
    assert(config != NULL && config->ctrl_if != NULL &&
           config->gpio_if != NULL);
    ++s_state.codec_create_calls;
    s_state.codec_mclk_multiple = config->mclk_div;
    s_state.codec_pa_voltage = config->hw_gain.pa_voltage;
    s_state.codec_dac_voltage = config->hw_gain.codec_dac_voltage;
    s_state.codec_pa_gain_db = config->hw_gain.pa_gain;
    return (const audio_codec_if_t *)&s_codec_interface;
}

esp_codec_dev_handle_t esp_codec_dev_new(esp_codec_dev_cfg_t *config)
{
    assert(config != NULL && config->codec_if != NULL &&
           config->data_if != NULL);
    return &s_codec_device;
}

void esp_codec_dev_delete(esp_codec_dev_handle_t device)
{
    assert(device == &s_codec_device);
    s_state.codec_opened = false;
    ++s_state.codec_delete_calls;
}

int esp_codec_dev_open(esp_codec_dev_handle_t device,
                       esp_codec_dev_sample_info_t *format)
{
    assert(device == &s_codec_device && format != NULL);
    ++s_state.codec_open_calls;
    if (s_state.codec_opened)
    {
        return ESP_CODEC_DEV_OK;
    }
    s_state.codec_opened = true;
    s_state.opened_format = *format;
    if (s_state.codec_open_failures_remaining > 0U)
    {
        --s_state.codec_open_failures_remaining;
        return ESP_FAIL;
    }
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_close(esp_codec_dev_handle_t device)
{
    assert(device == &s_codec_device);
    ++s_state.codec_close_calls;
    if (s_state.codec_close_failures_remaining > 0U)
    {
        --s_state.codec_close_failures_remaining;
        return ESP_FAIL;
    }
    s_state.codec_opened = false;
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t device, int volume)
{
    assert(device == &s_codec_device && volume >= 0 && volume <= 100);
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t device, bool muted)
{
    assert(device == &s_codec_device);
    (void)muted;
    return ESP_CODEC_DEV_OK;
}

int audio_codec_delete_codec_if(const audio_codec_if_t *interface)
{
    assert(interface == (const audio_codec_if_t *)&s_codec_interface);
    return ESP_CODEC_DEV_OK;
}

int audio_codec_delete_ctrl_if(const audio_codec_ctrl_if_t *interface)
{
    assert(interface == (const audio_codec_ctrl_if_t *)&s_ctrl_interface);
    return ESP_CODEC_DEV_OK;
}

int audio_codec_delete_gpio_if(const audio_codec_gpio_if_t *interface)
{
    assert(interface == (const audio_codec_gpio_if_t *)&s_gpio_interface);
    return ESP_CODEC_DEV_OK;
}

int audio_codec_delete_data_if(const audio_codec_data_if_t *interface)
{
    assert(interface == (const audio_codec_data_if_t *)&s_data_interface);
    return ESP_CODEC_DEV_OK;
}

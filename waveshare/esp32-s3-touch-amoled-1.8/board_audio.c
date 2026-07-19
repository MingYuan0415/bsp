#include <limits.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "board_audio.h"
#include "board_audio_format.h"

#define DBG_TAG "board_audio"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#ifndef CONFIG_BSP_AUDIO_I2S_PORT
    #define CONFIG_BSP_AUDIO_I2S_PORT 0
#endif
#ifndef CONFIG_BSP_AUDIO_I2S_MCLK_GPIO
    #define CONFIG_BSP_AUDIO_I2S_MCLK_GPIO 16
#endif
#ifndef CONFIG_BSP_AUDIO_I2S_BCLK_GPIO
    #define CONFIG_BSP_AUDIO_I2S_BCLK_GPIO 9
#endif
#ifndef CONFIG_BSP_AUDIO_I2S_LRCK_GPIO
    #define CONFIG_BSP_AUDIO_I2S_LRCK_GPIO 45
#endif
#ifndef CONFIG_BSP_AUDIO_I2S_DOUT_GPIO
    #define CONFIG_BSP_AUDIO_I2S_DOUT_GPIO 8
#endif
#ifndef CONFIG_BSP_AUDIO_I2S_DIN_GPIO
    #define CONFIG_BSP_AUDIO_I2S_DIN_GPIO 10
#endif
#ifndef CONFIG_BSP_AUDIO_PA_GPIO
    #define CONFIG_BSP_AUDIO_PA_GPIO 46
#endif
#ifndef CONFIG_BSP_AUDIO_SAMPLE_RATE_HZ
    #define CONFIG_BSP_AUDIO_SAMPLE_RATE_HZ 16000
#endif
#ifndef CONFIG_BSP_AUDIO_BITS_PER_SAMPLE
    #define CONFIG_BSP_AUDIO_BITS_PER_SAMPLE 16
#endif
#ifndef CONFIG_BSP_AUDIO_CHANNELS
    #define CONFIG_BSP_AUDIO_CHANNELS 2
#endif
#ifndef CONFIG_BSP_AUDIO_MCLK_MULTIPLE
    #define CONFIG_BSP_AUDIO_MCLK_MULTIPLE 384
#endif
#ifndef CONFIG_BSP_AUDIO_VOLUME_DEFAULT
    #define CONFIG_BSP_AUDIO_VOLUME_DEFAULT 60
#endif
#ifndef CONFIG_BSP_AUDIO_MIC_GAIN_DB
    #define CONFIG_BSP_AUDIO_MIC_GAIN_DB 30
#endif
#ifndef CONFIG_BSP_AUDIO_PA_DEFAULT_ON
    #define CONFIG_BSP_AUDIO_PA_DEFAULT_ON 1
#endif

#define BOARD_AUDIO_I2C_PORT (0)
#define BOARD_AUDIO_DEFAULT_MCLK_MULTIPLE (256U)
#define BOARD_AUDIO_CODEC_PA_REVERTED (false)
#define BOARD_AUDIO_CODEC_PA_VOLTAGE (3.3f)
#define BOARD_AUDIO_CODEC_DAC_VOLTAGE (3.3f)
#define BOARD_AUDIO_CODEC_PA_GAIN_DB (6.02f)

typedef struct board_audio_context
{
    SemaphoreHandle_t lock;
    SemaphoreHandle_t tx_lock;
    SemaphoreHandle_t rx_lock;
    void *i2c_bus;
    i2s_chan_handle_t tx_channel;
    i2s_chan_handle_t rx_channel;
    const audio_codec_data_if_t *data_if;
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    const audio_codec_if_t *codec_if;
    esp_codec_dev_handle_t codec_device;
    bsp_audio_config_t config;
    uint8_t volume;
    bool muted;
    bool pa_enabled;
    bool initialized;
    bool started;
    bool codec_close_required;
    bool pa_shutdown_pending;
} board_audio_context_t;

static board_audio_context_t s_audio;

static esp_err_t _codec_error(int result)
{
    return result == ESP_CODEC_DEV_OK ? ESP_OK : (esp_err_t)result;
}

static TickType_t _timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == UINT32_MAX)
    {
        return portMAX_DELAY;
    }

    uint64_t ticks = ((uint64_t)timeout_ms * configTICK_RATE_HZ + 999U) / 1000U;
    if (ticks == 0U && timeout_ms != 0U)
    {
        ticks = 1U;
    }
    if (ticks >= (uint64_t)portMAX_DELAY)
    {
        return portMAX_DELAY - 1U;
    }
    return (TickType_t)ticks;
}

static esp_err_t _lock_audio(void)
{
    if (s_audio.lock == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return xSemaphoreTake(s_audio.lock, portMAX_DELAY) == pdTRUE ?
           ESP_OK : ESP_ERR_TIMEOUT;
}

static void _unlock_audio(void)
{
    if (s_audio.lock != NULL)
    {
        (void)xSemaphoreGive(s_audio.lock);
    }
}

static esp_err_t _lock_streams_locked(void)
{
    if (xSemaphoreTake(s_audio.tx_lock, portMAX_DELAY) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
    if (xSemaphoreTake(s_audio.rx_lock, portMAX_DELAY) != pdTRUE)
    {
        (void)xSemaphoreGive(s_audio.tx_lock);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void _unlock_streams(void)
{
    (void)xSemaphoreGive(s_audio.rx_lock);
    (void)xSemaphoreGive(s_audio.tx_lock);
}

static bool _audio_config_valid(const bsp_audio_config_t *config)
{
    return config != NULL && board_audio_format_is_supported(
               config->sample_rate_hz, config->bits_per_sample,
               config->channels, config->mclk_multiple);
}

static uint16_t _effective_mclk_multiple(const bsp_audio_config_t *config)
{
    return config->mclk_multiple == 0U ?
           BOARD_AUDIO_DEFAULT_MCLK_MULTIPLE : config->mclk_multiple;
}

static i2s_std_config_t _make_i2s_config(const bsp_audio_config_t *config)
{
    const i2s_slot_mode_t slot_mode = config->channels == 1U ?
                                      I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
    i2s_std_config_t standard_config =
    {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(config->sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            (i2s_data_bit_width_t)config->bits_per_sample, slot_mode),
        .gpio_cfg =
        {
            .mclk = CONFIG_BSP_AUDIO_I2S_MCLK_GPIO,
            .bclk = CONFIG_BSP_AUDIO_I2S_BCLK_GPIO,
            .ws = CONFIG_BSP_AUDIO_I2S_LRCK_GPIO,
            .dout = CONFIG_BSP_AUDIO_I2S_DOUT_GPIO,
            .din = CONFIG_BSP_AUDIO_I2S_DIN_GPIO,
            .invert_flags =
            {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    standard_config.clk_cfg.mclk_multiple =
        (i2s_mclk_multiple_t)_effective_mclk_multiple(config);
    return standard_config;
}

static bool _audio_has_stream_resources_locked(void)
{
    return s_audio.tx_channel != NULL || s_audio.rx_channel != NULL ||
           s_audio.data_if != NULL ||
           s_audio.ctrl_if != NULL || s_audio.gpio_if != NULL ||
           s_audio.codec_if != NULL || s_audio.codec_device != NULL;
}

static bool _audio_stream_resources_complete_locked(void)
{
    return s_audio.tx_channel != NULL && s_audio.rx_channel != NULL &&
           s_audio.data_if != NULL && s_audio.ctrl_if != NULL &&
           s_audio.gpio_if != NULL && s_audio.codec_if != NULL &&
           s_audio.codec_device != NULL;
}

static bool _audio_has_hardware_resources_locked(void)
{
    return s_audio.i2c_bus != NULL || _audio_has_stream_resources_locked() ||
           s_audio.codec_close_required || s_audio.pa_shutdown_pending;
}

static void _delete_audio_locks(void)
{
    if (s_audio.rx_lock != NULL)
    {
        vSemaphoreDelete(s_audio.rx_lock);
        s_audio.rx_lock = NULL;
    }
    if (s_audio.tx_lock != NULL)
    {
        vSemaphoreDelete(s_audio.tx_lock);
        s_audio.tx_lock = NULL;
    }
    if (s_audio.lock != NULL)
    {
        vSemaphoreDelete(s_audio.lock);
        s_audio.lock = NULL;
    }
}

static esp_err_t _set_pa_level(bool enabled)
{
#if CONFIG_BSP_AUDIO_PA_GPIO >= 0
    s_audio.pa_shutdown_pending = true;
    esp_err_t result = gpio_set_level(
                           (gpio_num_t)CONFIG_BSP_AUDIO_PA_GPIO,
                           enabled == BOARD_AUDIO_CODEC_PA_REVERTED ? 0 : 1);
    if (!enabled && result == ESP_OK)
    {
        s_audio.pa_shutdown_pending = false;
    }
    return result;
#else
    (void)enabled;
    s_audio.pa_shutdown_pending = false;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t _configure_pa_gpio(void)
{
#if CONFIG_BSP_AUDIO_PA_GPIO >= 0
    s_audio.pa_shutdown_pending = true;
    const gpio_config_t config =
    {
        .pin_bit_mask = (1ULL << CONFIG_BSP_AUDIO_PA_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t result = gpio_config(&config);
    if (result != ESP_OK)
    {
        return result;
    }
    return _set_pa_level(false);
#else
    s_audio.pa_shutdown_pending = false;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t _destroy_codec_locked(void)
{
    esp_err_t first_error = ESP_OK;
    if (s_audio.codec_device != NULL)
    {
        esp_codec_dev_delete(s_audio.codec_device);
        s_audio.codec_device = NULL;
        s_audio.codec_close_required = false;
    }
    if (s_audio.codec_if != NULL)
    {
        esp_err_t result = _codec_error(audio_codec_delete_codec_if(s_audio.codec_if));
        if (first_error == ESP_OK && result != ESP_OK)
        {
            first_error = result;
        }
        s_audio.codec_if = NULL;
    }
    if (s_audio.gpio_if != NULL)
    {
        esp_err_t result = _codec_error(audio_codec_delete_gpio_if(s_audio.gpio_if));
        if (first_error == ESP_OK && result != ESP_OK)
        {
            first_error = result;
        }
        s_audio.gpio_if = NULL;
    }
    if (s_audio.ctrl_if != NULL)
    {
        esp_err_t result = _codec_error(audio_codec_delete_ctrl_if(s_audio.ctrl_if));
        if (first_error == ESP_OK && result != ESP_OK)
        {
            first_error = result;
        }
        s_audio.ctrl_if = NULL;
    }
    return first_error;
}

static esp_err_t _stop_audio_locked(void)
{
    esp_err_t result = ESP_OK;
    if (s_audio.started || s_audio.codec_close_required)
    {
        const esp_err_t close_result = s_audio.codec_device == NULL ?
                                       ESP_ERR_INVALID_STATE :
                                       _codec_error(esp_codec_dev_close(
                                               s_audio.codec_device));
        if (close_result == ESP_OK)
        {
            s_audio.codec_close_required = false;
        }
        else
        {
            result = close_result;
        }
        s_audio.started = false;
    }
    if (s_audio.pa_shutdown_pending)
    {
        esp_err_t pa_result = _set_pa_level(false);
        if (pa_result == ESP_ERR_NOT_SUPPORTED)
        {
            pa_result = ESP_OK;
        }
        if (result == ESP_OK && pa_result != ESP_OK)
        {
            result = pa_result;
        }
    }

    const i2s_chan_handle_t channels[] =
    {
        s_audio.tx_channel,
        s_audio.rx_channel,
    };
    for (size_t index = 0U;
            index < sizeof(channels) / sizeof(channels[0]); ++index)
    {
        if (channels[index] == NULL)
        {
            continue;
        }
        i2s_chan_info_t info = {0};
        esp_err_t channel_result = i2s_channel_get_info(channels[index], &info);
        if (channel_result == ESP_OK && info.is_enabled)
        {
            channel_result = i2s_channel_disable(channels[index]);
        }
        if (result == ESP_OK && channel_result != ESP_OK)
        {
            result = channel_result;
        }
    }
    return result;
}

static esp_err_t _create_codec_locked(void)
{
    audio_codec_i2c_cfg_t i2c_config =
    {
        .port = BOARD_AUDIO_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_audio.i2c_bus,
    };
    s_audio.ctrl_if = audio_codec_new_i2c_ctrl(&i2c_config);
    if (s_audio.ctrl_if == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    s_audio.gpio_if = audio_codec_new_gpio();
    if (s_audio.gpio_if == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    es8311_codec_cfg_t codec_config =
    {
        .ctrl_if = s_audio.ctrl_if,
        .gpio_if = s_audio.gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = CONFIG_BSP_AUDIO_PA_GPIO,
        .pa_reverted = BOARD_AUDIO_CODEC_PA_REVERTED,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain =
        {
            .pa_voltage = BOARD_AUDIO_CODEC_PA_VOLTAGE,
            .codec_dac_voltage = BOARD_AUDIO_CODEC_DAC_VOLTAGE,
            .pa_gain = BOARD_AUDIO_CODEC_PA_GAIN_DB,
        },
        .no_dac_ref = false,
        .mclk_div = _effective_mclk_multiple(&s_audio.config),
    };
    s_audio.codec_if = es8311_codec_new(&codec_config);
    if (s_audio.codec_if == NULL)
    {
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t device_config =
    {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = s_audio.codec_if,
        .data_if = s_audio.data_if,
    };
    s_audio.codec_device = esp_codec_dev_new(&device_config);
    s_audio.codec_close_required = false;
    return s_audio.codec_device != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t _init_i2s_locked(void)
{
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    channel_config.auto_clear_after_cb = true;
    esp_err_t result = i2s_new_channel(&channel_config,
                                       &s_audio.tx_channel,
                                       &s_audio.rx_channel);
    if (result != ESP_OK)
    {
        return result;
    }

    const i2s_std_config_t standard_config = _make_i2s_config(&s_audio.config);

    result = i2s_channel_init_std_mode(s_audio.tx_channel, &standard_config);
    if (result != ESP_OK)
    {
        return result;
    }
    result = i2s_channel_init_std_mode(s_audio.rx_channel, &standard_config);
    return result;
}

static esp_err_t _create_stream_resources_locked(void)
{
    if (_audio_stream_resources_complete_locked())
    {
        return ESP_OK;
    }
    if (_audio_has_stream_resources_locked() || s_audio.i2c_bus == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = _init_i2s_locked();
    if (result != ESP_OK)
    {
        return result;
    }

    audio_codec_i2s_cfg_t data_config =
    {
        .port = CONFIG_BSP_AUDIO_I2S_PORT,
        .rx_handle = s_audio.rx_channel,
        .tx_handle = s_audio.tx_channel,
        .clk_src = 0,
    };
    s_audio.data_if = audio_codec_new_i2s_data(&data_config);
    if (s_audio.data_if == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    return _create_codec_locked();
}

static esp_err_t _release_stream_resources_locked(void)
{
    esp_err_t result = _stop_audio_locked();
    if (result != ESP_OK)
    {
        return result;
    }

    esp_err_t first_error = _destroy_codec_locked();
    result = ESP_OK;
    if (s_audio.data_if != NULL)
    {
        result = _codec_error(audio_codec_delete_data_if(s_audio.data_if));
        if (first_error == ESP_OK && result != ESP_OK)
        {
            first_error = result;
        }
        s_audio.data_if = NULL;
    }
    if (s_audio.rx_channel != NULL)
    {
        result = i2s_del_channel(s_audio.rx_channel);
        if (first_error == ESP_OK && result != ESP_OK)
        {
            first_error = result;
        }
        if (result == ESP_OK)
        {
            s_audio.rx_channel = NULL;
        }
    }
    if (s_audio.tx_channel != NULL)
    {
        result = i2s_del_channel(s_audio.tx_channel);
        if (first_error == ESP_OK && result != ESP_OK)
        {
            first_error = result;
        }
        if (result == ESP_OK)
        {
            s_audio.tx_channel = NULL;
        }
    }
    return first_error;
}

static esp_err_t _verify_stream_running_locked(void)
{
    const i2s_chan_handle_t channels[] =
    {
        s_audio.tx_channel,
        s_audio.rx_channel,
    };
    for (size_t index = 0U;
            index < sizeof(channels) / sizeof(channels[0]); ++index)
    {
        i2s_chan_info_t info = {0};
        esp_err_t result = i2s_channel_get_info(channels[index], &info);
        if (result != ESP_OK)
        {
            return result;
        }
        if (!info.is_enabled)
        {
            return ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_OK;
}

static esp_err_t _cleanup_audio_locked(void)
{
    esp_err_t first_error = _release_stream_resources_locked();
    if (!_audio_has_stream_resources_locked())
    {
        s_audio.i2c_bus = NULL;
    }
    s_audio.initialized = false;
    s_audio.started = false;
    s_audio.config = bsp_audio_get_default_config();
    s_audio.volume = CONFIG_BSP_AUDIO_VOLUME_DEFAULT > 100 ?
                     100 : CONFIG_BSP_AUDIO_VOLUME_DEFAULT;
    s_audio.muted = false;
    s_audio.pa_enabled = CONFIG_BSP_AUDIO_PA_DEFAULT_ON;
    return first_error;
}

static esp_err_t _apply_output_settings_locked(void)
{
    esp_err_t result = _codec_error(esp_codec_dev_set_out_vol(
                                        s_audio.codec_device, s_audio.volume));
    if (result != ESP_OK)
    {
        return result;
    }
    result = _codec_error(esp_codec_dev_set_out_mute(
                              s_audio.codec_device, s_audio.muted));
    if (result != ESP_OK)
    {
        return result;
    }
    result = _set_pa_level(s_audio.pa_enabled);
    return result == ESP_ERR_NOT_SUPPORTED ? ESP_OK : result;
}

bsp_audio_config_t bsp_audio_get_default_config(void)
{
    const bsp_audio_config_t config =
    {
        .sample_rate_hz = CONFIG_BSP_AUDIO_SAMPLE_RATE_HZ,
        .bits_per_sample = CONFIG_BSP_AUDIO_BITS_PER_SAMPLE,
        .channels = CONFIG_BSP_AUDIO_CHANNELS,
        .mclk_multiple = CONFIG_BSP_AUDIO_MCLK_MULTIPLE,
    };
    return config;
}

esp_err_t board_audio_init(void *i2c_bus)
{
    if (i2c_bus == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_audio.lock == NULL)
    {
        s_audio.lock = xSemaphoreCreateMutex();
        if (s_audio.lock == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_audio.tx_lock == NULL)
    {
        s_audio.tx_lock = xSemaphoreCreateMutex();
        if (s_audio.tx_lock == NULL)
        {
            _delete_audio_locks();
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_audio.rx_lock == NULL)
    {
        s_audio.rx_lock = xSemaphoreCreateMutex();
        if (s_audio.rx_lock == NULL)
        {
            _delete_audio_locks();
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t result = _lock_audio();
    if (result != ESP_OK)
    {
        return result;
    }
    if (s_audio.initialized)
    {
        _unlock_audio();
        return ESP_OK;
    }
    if (_audio_has_hardware_resources_locked())
    {
        result = _cleanup_audio_locked();
        if (result != ESP_OK)
        {
            goto exit;
        }
    }

    s_audio.i2c_bus = i2c_bus;
    s_audio.config = bsp_audio_get_default_config();
    if (!_audio_config_valid(&s_audio.config))
    {
        result = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }
    s_audio.volume = CONFIG_BSP_AUDIO_VOLUME_DEFAULT > 100 ?
                     100 : CONFIG_BSP_AUDIO_VOLUME_DEFAULT;
    s_audio.muted = false;
    s_audio.pa_enabled = CONFIG_BSP_AUDIO_PA_DEFAULT_ON;

    result = _configure_pa_gpio();
    if (result != ESP_OK && result != ESP_ERR_NOT_SUPPORTED)
    {
        goto cleanup;
    }
    s_audio.initialized = true;
    result = ESP_OK;
    goto exit;

cleanup:
    {
        const esp_err_t cleanup_result = _cleanup_audio_locked();
        if (cleanup_result != ESP_OK)
        {
            result = cleanup_result;
        }
    }

exit:
    {
        const bool release_locks = !s_audio.initialized &&
                                   !_audio_has_hardware_resources_locked();
        _unlock_audio();
        if (release_locks)
        {
            _delete_audio_locks();
        }
        return result;
    }
}

esp_err_t board_audio_deinit(void)
{
    if (s_audio.lock == NULL)
    {
        return ESP_OK;
    }
    esp_err_t result = _lock_audio();
    if (result != ESP_OK)
    {
        return result;
    }
    if (!s_audio.initialized && !_audio_has_hardware_resources_locked())
    {
        _unlock_audio();
        _delete_audio_locks();
        return ESP_OK;
    }
    result = _lock_streams_locked();
    if (result != ESP_OK)
    {
        _unlock_audio();
        return result;
    }
    result = _cleanup_audio_locked();
    _unlock_streams();
    const bool release_locks = !_audio_has_hardware_resources_locked();
    _unlock_audio();
    if (release_locks)
    {
        _delete_audio_locks();
    }
    return result;
}

esp_err_t bsp_audio_init(void *i2c_bus)
{
    return board_audio_init(i2c_bus);
}

esp_err_t bsp_audio_deinit(void)
{
    return board_audio_deinit();
}

bool bsp_audio_is_available(void)
{
    if (_lock_audio() != ESP_OK)
    {
        return false;
    }
    const bool available = s_audio.initialized && s_audio.i2c_bus != NULL;
    _unlock_audio();
    return available;
}

bool bsp_audio_is_started(void)
{
    if (_lock_audio() != ESP_OK)
    {
        return false;
    }
    const bool started = s_audio.started;
    _unlock_audio();
    return started;
}

esp_err_t bsp_audio_configure(const bsp_audio_config_t *config)
{
    if (!_audio_config_valid(config))
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = _lock_audio();
    if (result != ESP_OK)
    {
        return result;
    }
    if (!s_audio.initialized)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    if (s_audio.started || s_audio.codec_close_required ||
            s_audio.pa_shutdown_pending)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    s_audio.config = *config;
    result = ESP_OK;

exit:
    _unlock_audio();
    return result;
}

esp_err_t bsp_audio_start(void)
{
    esp_err_t result = _lock_audio();
    if (result != ESP_OK)
    {
        return result;
    }
    if (!s_audio.initialized)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    if (s_audio.started)
    {
        result = ESP_OK;
        goto exit;
    }
    if (s_audio.codec_close_required || s_audio.pa_shutdown_pending ||
            _audio_has_stream_resources_locked())
    {
        result = _release_stream_resources_locked();
        if (result != ESP_OK)
        {
            goto exit;
        }
    }
    if (!_audio_stream_resources_complete_locked())
    {
        result = _create_stream_resources_locked();
        if (result != ESP_OK)
        {
            const esp_err_t cleanup_result =
                _release_stream_resources_locked();
            if (cleanup_result != ESP_OK)
            {
                result = cleanup_result;
            }
            goto exit;
        }
    }

    esp_codec_dev_sample_info_t sample_info =
    {
        .bits_per_sample = s_audio.config.bits_per_sample,
        .channel = s_audio.config.channels,
        .channel_mask = 0,
        .sample_rate = s_audio.config.sample_rate_hz,
        .mclk_multiple = _effective_mclk_multiple(&s_audio.config),
    };
#if CONFIG_BSP_AUDIO_PA_GPIO >= 0
    s_audio.pa_shutdown_pending = true;
#endif
    s_audio.codec_close_required = true;
    result = _codec_error(esp_codec_dev_open(s_audio.codec_device,
                          &sample_info));
    if (result != ESP_OK)
    {
        const esp_err_t cleanup_result = _release_stream_resources_locked();
        if (cleanup_result != ESP_OK)
        {
            result = cleanup_result;
        }
        goto exit;
    }

    result = _verify_stream_running_locked();
    if (result != ESP_OK)
    {
        const esp_err_t cleanup_result = _release_stream_resources_locked();
        if (cleanup_result != ESP_OK)
        {
            result = cleanup_result;
        }
        goto exit;
    }

    result = _codec_error(esp_codec_dev_set_in_gain(
                              s_audio.codec_device,
                              (float)CONFIG_BSP_AUDIO_MIC_GAIN_DB));
    if (result != ESP_OK)
    {
        const esp_err_t cleanup_result = _release_stream_resources_locked();
        if (cleanup_result != ESP_OK)
        {
            result = cleanup_result;
        }
        goto exit;
    }

    s_audio.started = true;
    result = _apply_output_settings_locked();
    if (result != ESP_OK)
    {
        const esp_err_t cleanup_result = _release_stream_resources_locked();
        if (cleanup_result != ESP_OK)
        {
            result = cleanup_result;
        }
    }

exit:
    _unlock_audio();
    return result;
}

esp_err_t bsp_audio_stop(void)
{
    esp_err_t result = _lock_audio();
    if (result != ESP_OK)
    {
        return result;
    }
    if (!s_audio.initialized)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    if (!s_audio.started && !s_audio.codec_close_required &&
            !s_audio.pa_shutdown_pending &&
            !_audio_has_stream_resources_locked())
    {
        result = ESP_OK;
        goto exit;
    }
    result = _lock_streams_locked();
    if (result != ESP_OK)
    {
        goto exit;
    }
    result = _release_stream_resources_locked();
    _unlock_streams();

exit:
    _unlock_audio();
    return result;
}

esp_err_t bsp_audio_write(void *data, size_t bytes, size_t *written,
                          uint32_t timeout_ms)
{
    if (data == NULL || bytes == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (written != NULL)
    {
        *written = 0U;
    }
    esp_err_t result = _lock_audio();
    if (result != ESP_OK)
    {
        return result;
    }
    if (!s_audio.started || s_audio.tx_channel == NULL)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    if (xSemaphoreTake(s_audio.tx_lock, portMAX_DELAY) != pdTRUE)
    {
        result = ESP_ERR_TIMEOUT;
        goto exit;
    }
    i2s_chan_handle_t channel = s_audio.tx_channel;
    _unlock_audio();

    size_t actual = 0U;
    result = i2s_channel_write(channel, data, bytes, &actual,
                               _timeout_to_ticks(timeout_ms));
    if (written != NULL)
    {
        *written = actual;
    }
    (void)xSemaphoreGive(s_audio.tx_lock);
    return result;

exit:
    _unlock_audio();
    return result;
}

esp_err_t bsp_audio_read(void *data, size_t bytes, size_t *read,
                         uint32_t timeout_ms)
{
    if (data == NULL || bytes == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (read != NULL)
    {
        *read = 0U;
    }
    esp_err_t result = _lock_audio();
    if (result != ESP_OK)
    {
        return result;
    }
    if (!s_audio.started || s_audio.rx_channel == NULL)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    if (xSemaphoreTake(s_audio.rx_lock, portMAX_DELAY) != pdTRUE)
    {
        result = ESP_ERR_TIMEOUT;
        goto exit;
    }
    i2s_chan_handle_t channel = s_audio.rx_channel;
    _unlock_audio();

    size_t actual = 0U;
    result = i2s_channel_read(channel, data, bytes, &actual,
                              _timeout_to_ticks(timeout_ms));
    if (read != NULL)
    {
        *read = actual;
    }
    (void)xSemaphoreGive(s_audio.rx_lock);
    return result;

exit:
    _unlock_audio();
    return result;
}

esp_err_t bsp_audio_set_volume(uint8_t percent)
{
    if (percent > 100U)
    {
        percent = 100U;
    }
    esp_err_t result = _lock_audio();
    if (result != ESP_OK)
    {
        return result;
    }
    if (!s_audio.initialized)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    s_audio.volume = percent;
    if (s_audio.started)
    {
        result = _codec_error(esp_codec_dev_set_out_vol(s_audio.codec_device,
                              percent));
    }

exit:
    _unlock_audio();
    return result;
}

esp_err_t bsp_audio_get_volume(uint8_t *percent)
{
    if (percent == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = _lock_audio();
    if (result != ESP_OK)
    {
        return result;
    }
    if (!s_audio.initialized)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    *percent = s_audio.volume;

exit:
    _unlock_audio();
    return result;
}

esp_err_t bsp_audio_set_mute(bool muted)
{
    esp_err_t result = _lock_audio();
    if (result != ESP_OK)
    {
        return result;
    }
    if (!s_audio.initialized)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    s_audio.muted = muted;
    if (s_audio.started)
    {
        result = _codec_error(esp_codec_dev_set_out_mute(s_audio.codec_device,
                              muted));
    }

exit:
    _unlock_audio();
    return result;
}

esp_err_t bsp_audio_get_mute(bool *muted)
{
    if (muted == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = _lock_audio();
    if (result != ESP_OK)
    {
        return result;
    }
    if (!s_audio.initialized)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    *muted = s_audio.muted;

exit:
    _unlock_audio();
    return result;
}

esp_err_t bsp_audio_set_pa(bool enabled)
{
    esp_err_t result = _lock_audio();
    if (result != ESP_OK)
    {
        return result;
    }
    if (!s_audio.initialized)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    result = _set_pa_level(s_audio.started && enabled);
    if (result == ESP_OK || result == ESP_ERR_NOT_SUPPORTED)
    {
        s_audio.pa_enabled = enabled;
        if (result == ESP_ERR_NOT_SUPPORTED)
        {
            result = ESP_OK;
        }
    }

exit:
    _unlock_audio();
    return result;
}

esp_err_t bsp_audio_get_pa(bool *enabled)
{
    if (enabled == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = _lock_audio();
    if (result != ESP_OK)
    {
        return result;
    }
    if (!s_audio.initialized)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    *enabled = s_audio.pa_enabled;

exit:
    _unlock_audio();
    return result;
}

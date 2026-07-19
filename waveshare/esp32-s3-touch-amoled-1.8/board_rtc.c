#include <time.h>

#include "board_rtc.h"

#include "esp_err.h"

#include "bsp_hal.h"
#include "mt_pcf85063.h"

#define DBG_TAG "board_rtc"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

static mt_pcf85063_t *s_rtc_device = NULL;
static board_tca9554_t *s_io_expander = NULL;
static uint8_t s_alarm_pin_mask = 0U;
static bool s_rtc_registered = false;

static bool _board_rtc_is_available(void)
{
    return mt_pcf85063_is_ready(s_rtc_device);
}

bool board_rtc_has_resources(void)
{
    return s_rtc_device != NULL || s_io_expander != NULL;
}

static esp_err_t _board_rtc_read_time(struct tm *timeinfo)
{
    if (!timeinfo)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!_board_rtc_is_available())
    {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return mt_pcf85063_get_time(s_rtc_device, timeinfo);
}

static esp_err_t _board_rtc_write_time(const struct tm *timeinfo)
{
    if (!timeinfo)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!_board_rtc_is_available())
    {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return mt_pcf85063_set_time(s_rtc_device, timeinfo);
}

static esp_err_t _board_rtc_alarm_configure(
    const bsp_rtc_alarm_config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!_board_rtc_is_available())
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    const mt_pcf85063_alarm_config_t driver_config =
    {
        .match_second = config->match_second,
        .second = config->second,
        .match_minute = config->match_minute,
        .minute = config->minute,
        .match_hour = config->match_hour,
        .hour = config->hour,
        .match_day = config->match_day,
        .day = config->day,
        .match_weekday = config->match_weekday,
        .weekday = config->weekday,
    };
    return mt_pcf85063_alarm_configure(s_rtc_device, &driver_config);
}

static esp_err_t _board_rtc_alarm_disable(void)
{
    return _board_rtc_is_available() ?
           mt_pcf85063_alarm_disable(s_rtc_device) : ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t _board_rtc_alarm_get_status(bsp_rtc_alarm_status_t *status)
{
    if (status == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!_board_rtc_is_available())
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    mt_pcf85063_alarm_status_t driver_status;
    esp_err_t result = mt_pcf85063_alarm_get_status(
                           s_rtc_device, &driver_status);
    if (result == ESP_OK)
    {
        *status = (bsp_rtc_alarm_status_t)
        {
            .enabled = driver_status.enabled,
            .pending = driver_status.pending,
        };
    }
    return result;
}

static esp_err_t _board_rtc_alarm_clear(void)
{
    return _board_rtc_is_available() ?
           mt_pcf85063_alarm_clear(s_rtc_device) : ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t _board_rtc_alarm_poll_interrupt(bool *active)
{
    if (active == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!_board_rtc_is_available() || s_io_expander == NULL ||
            s_alarm_pin_mask == 0U)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t level = 0U;
    esp_err_t result = board_tca9554_get_input_level(
                           s_io_expander, s_alarm_pin_mask, &level);
    if (result == ESP_OK)
    {
        *active = level == 0U;
    }
    return result;
}

static const bsp_rtc_ops_t s_rtc_ops =
{
    .is_available = _board_rtc_is_available,
    .read = _board_rtc_read_time,
    .write = _board_rtc_write_time,
    .alarm_configure = _board_rtc_alarm_configure,
    .alarm_disable = _board_rtc_alarm_disable,
    .alarm_get_status = _board_rtc_alarm_get_status,
    .alarm_clear = _board_rtc_alarm_clear,
    .alarm_poll_interrupt = _board_rtc_alarm_poll_interrupt,
};

esp_err_t board_rtc_init(i2c_master_bus_handle_t i2c_bus,
                         board_tca9554_t *io_expander,
                         uint8_t alarm_pin_mask)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (i2c_bus == NULL || io_expander == NULL || alarm_pin_mask == 0U ||
            (alarm_pin_mask & (uint8_t)(alarm_pin_mask - 1U)) != 0U)
    {
        return result;
    }

    if (s_rtc_device != NULL && !mt_pcf85063_is_ready(s_rtc_device))
    {
        esp_err_t cleanup_ret = mt_pcf85063_destroy(s_rtc_device);
        if (cleanup_ret != ESP_OK)
        {
            return cleanup_ret;
        }
        s_rtc_device = NULL;
        s_io_expander = NULL;
        s_alarm_pin_mask = 0U;
        s_rtc_registered = false;
    }
    if (s_rtc_device != NULL && s_rtc_registered)
    {
        return s_io_expander == io_expander &&
               s_alarm_pin_mask == alarm_pin_mask ? ESP_OK :
               ESP_ERR_INVALID_STATE;
    }

    result = ESP_OK;
    if (s_rtc_device == NULL)
    {
        result = mt_pcf85063_create(i2c_bus, &s_rtc_device);
        if (result != ESP_OK)
        {
            LOG_E("pcf85063 create failed: %s", esp_err_to_name(result));
            return result;
        }
    }

    s_io_expander = io_expander;
    s_alarm_pin_mask = alarm_pin_mask;

    result = bsp_hal_register_rtc(&s_rtc_ops);
    if (result != ESP_OK)
    {
        LOG_E("rtc ops register failed: %s", esp_err_to_name(result));
        esp_err_t destroy_ret = mt_pcf85063_destroy(s_rtc_device);
        if (destroy_ret == ESP_OK)
        {
            s_rtc_device = NULL;
            s_io_expander = NULL;
            s_alarm_pin_mask = 0U;
        }
        s_rtc_registered = false;
        return result;
    }

    s_rtc_registered = true;
    LOG_I("pcf85063 rtc ready");

    return result;
}

esp_err_t board_rtc_deinit(void)
{
    if (s_rtc_device == NULL)
    {
        return ESP_OK;
    }

    esp_err_t result = mt_pcf85063_destroy(s_rtc_device);
    if (result == ESP_OK)
    {
        s_rtc_device = NULL;
        s_io_expander = NULL;
        s_alarm_pin_mask = 0U;
        s_rtc_registered = false;
    }
    return result;
}

#include <time.h>

#include "board_rtc.h"

#include "esp_err.h"

#include "bsp_hal.h"
#include "mt_pcf85063.h"

#define DBG_TAG "board_rtc"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

static mt_pcf85063_t *s_rtc_device = NULL;
static bool s_rtc_registered = false;

static bool _board_rtc_is_available(void)
{
    return mt_pcf85063_is_ready(s_rtc_device);
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

static const bsp_rtc_ops_t s_rtc_ops =
{
    .is_available = _board_rtc_is_available,
    .read = _board_rtc_read_time,
    .write = _board_rtc_write_time,
};

esp_err_t board_rtc_init(i2c_master_bus_handle_t i2c_bus)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (!i2c_bus)
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
        s_rtc_registered = false;
    }
    if (s_rtc_device != NULL && s_rtc_registered)
    {
        return ESP_OK;
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

    result = bsp_hal_register_rtc(&s_rtc_ops);
    if (result != ESP_OK)
    {
        LOG_E("rtc ops register failed: %s", esp_err_to_name(result));
        esp_err_t destroy_ret = mt_pcf85063_destroy(s_rtc_device);
        if (destroy_ret == ESP_OK)
        {
            s_rtc_device = NULL;
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
        s_rtc_registered = false;
    }
    return result;
}

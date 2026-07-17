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
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (!timeinfo)
    {
        goto exit;
    }

    if (!_board_rtc_is_available())
    {
        result = ESP_ERR_NOT_SUPPORTED;
        goto exit;
    }
    result = mt_pcf85063_get_time(s_rtc_device, timeinfo);

exit:
    return result;
}

static esp_err_t _board_rtc_write_time(const struct tm *timeinfo)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (!timeinfo)
    {
        goto exit;
    }

    if (!_board_rtc_is_available())
    {
        result = ESP_ERR_NOT_SUPPORTED;
        goto exit;
    }
    result = mt_pcf85063_set_time(s_rtc_device, timeinfo);

exit:
    return result;
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
        goto exit;
    }

    if (s_rtc_device != NULL && !mt_pcf85063_is_ready(s_rtc_device))
    {
        esp_err_t cleanup_ret = mt_pcf85063_destroy(s_rtc_device);
        if (cleanup_ret != ESP_OK)
        {
            result = cleanup_ret;
            goto exit;
        }
        s_rtc_device = NULL;
        s_rtc_registered = false;
    }
    if (s_rtc_device != NULL && s_rtc_registered)
    {
        result = ESP_OK;
        goto exit;
    }

    result = ESP_OK;
    if (s_rtc_device == NULL)
    {
        result = mt_pcf85063_create(i2c_bus, &s_rtc_device);
        if (result != ESP_OK)
        {
            LOG_E("pcf85063 create failed: %s", esp_err_to_name(result));
            goto exit;
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
        goto exit;
    }

    s_rtc_registered = true;
    LOG_I("pcf85063 rtc ready");

exit:
    return result;
}

esp_err_t board_rtc_deinit(void)
{
    esp_err_t result = ESP_OK;
    if (s_rtc_device == NULL)
    {
        goto exit;
    }

    result = mt_pcf85063_destroy(s_rtc_device);
    if (result == ESP_OK)
    {
        s_rtc_device = NULL;
        s_rtc_registered = false;
    }

exit:
    return result;
}

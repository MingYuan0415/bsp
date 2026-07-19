#include "board_rtc.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp_hal.h"
#include "mt_pcf85063.h"

struct board_tca9554
{
    uint8_t input;
};

struct mt_pcf85063
{
    bool ready;
    mt_pcf85063_alarm_config_t config;
    mt_pcf85063_alarm_status_t status;
};

static struct mt_pcf85063 s_rtc;
static const bsp_rtc_ops_t *s_registered_ops;
static esp_err_t s_destroy_result = ESP_OK;

const char *esp_err_to_name(esp_err_t error)
{
    (void)error;
    return "fake";
}

esp_err_t bsp_hal_register_rtc(const bsp_rtc_ops_t *ops)
{
    assert(ops != NULL);
    s_registered_ops = ops;
    return ESP_OK;
}

esp_err_t board_tca9554_get_input_level(board_tca9554_t *device,
                                        uint8_t pin_mask,
                                        uint8_t *level_mask)
{
    assert(device != NULL && pin_mask != 0U && level_mask != NULL);
    *level_mask = (uint8_t)(device->input & pin_mask);
    return ESP_OK;
}

esp_err_t mt_pcf85063_create(i2c_master_bus_handle_t i2c_bus,
                             mt_pcf85063_t **out_device)
{
    assert(i2c_bus != NULL && out_device != NULL);
    memset(&s_rtc, 0, sizeof(s_rtc));
    s_rtc.ready = true;
    *out_device = &s_rtc;
    return ESP_OK;
}

esp_err_t mt_pcf85063_destroy(mt_pcf85063_t *device)
{
    assert(device == &s_rtc);
    if (s_destroy_result == ESP_OK)
    {
        s_rtc.ready = false;
    }
    return s_destroy_result;
}

bool mt_pcf85063_is_ready(const mt_pcf85063_t *device)
{
    return device == &s_rtc && s_rtc.ready;
}

esp_err_t mt_pcf85063_get_time(mt_pcf85063_t *device, struct tm *timeinfo)
{
    assert(device == &s_rtc && timeinfo != NULL);
    memset(timeinfo, 0, sizeof(*timeinfo));
    return ESP_OK;
}

esp_err_t mt_pcf85063_set_time(mt_pcf85063_t *device,
                               const struct tm *timeinfo)
{
    assert(device == &s_rtc && timeinfo != NULL);
    return ESP_OK;
}

esp_err_t mt_pcf85063_alarm_configure(
    mt_pcf85063_t *device, const mt_pcf85063_alarm_config_t *config)
{
    assert(device == &s_rtc && config != NULL);
    s_rtc.config = *config;
    s_rtc.status.enabled = true;
    s_rtc.status.pending = false;
    return ESP_OK;
}

esp_err_t mt_pcf85063_alarm_disable(mt_pcf85063_t *device)
{
    assert(device == &s_rtc);
    memset(&s_rtc.status, 0, sizeof(s_rtc.status));
    return ESP_OK;
}

esp_err_t mt_pcf85063_alarm_get_status(
    mt_pcf85063_t *device, mt_pcf85063_alarm_status_t *status)
{
    assert(device == &s_rtc && status != NULL);
    *status = s_rtc.status;
    return ESP_OK;
}

esp_err_t mt_pcf85063_alarm_clear(mt_pcf85063_t *device)
{
    assert(device == &s_rtc);
    s_rtc.status.pending = false;
    return ESP_OK;
}

int main(void)
{
    struct board_tca9554 expander = {0};
    const uint8_t rtc_int_mask = 1U << 3;
    assert(!board_rtc_has_resources());
    assert(board_rtc_init((i2c_master_bus_handle_t)&s_rtc,
                          &expander, rtc_int_mask) == ESP_OK);
    assert(board_rtc_has_resources());
    assert(s_registered_ops != NULL && s_registered_ops->is_available());

    bool active = false;
    expander.input = rtc_int_mask;
    assert(s_registered_ops->alarm_poll_interrupt(&active) == ESP_OK);
    assert(!active);
    expander.input = 0U;
    assert(s_registered_ops->alarm_poll_interrupt(&active) == ESP_OK);
    assert(active);

    const bsp_rtc_alarm_config_t config =
    {
        .match_minute = true,
        .minute = 12U,
    };
    assert(s_registered_ops->alarm_configure(&config) == ESP_OK);
    assert(s_rtc.config.match_minute && s_rtc.config.minute == 12U);
    bsp_rtc_alarm_status_t status;
    assert(s_registered_ops->alarm_get_status(&status) == ESP_OK);
    assert(status.enabled && !status.pending);
    s_rtc.status.pending = true;
    assert(s_registered_ops->alarm_clear() == ESP_OK);
    assert(!s_rtc.status.pending);
    assert(s_registered_ops->alarm_disable() == ESP_OK);
    assert(!s_rtc.status.enabled);

    s_destroy_result = ESP_FAIL;
    assert(board_rtc_deinit() == ESP_FAIL);
    assert(board_rtc_has_resources());
    s_destroy_result = ESP_OK;
    assert(board_rtc_deinit() == ESP_OK);
    assert(!board_rtc_has_resources());
    puts("board RTC alarm regression passed");
    return 0;
}

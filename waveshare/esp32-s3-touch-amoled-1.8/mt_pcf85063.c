#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "driver/i2c_master.h"
#include "esp_err.h"

#include "mt_pcf85063.h"

#define MT_PCF85063_I2C_ADDR           (0x51)
#define MT_PCF85063_I2C_TIMEOUT_MS     (20)
#define MT_PCF85063_I2C_SPEED_HZ       (100000)

#define MT_PCF85063_REG_CONTROL_1      (0x00)
#define MT_PCF85063_REG_SECONDS        (0x04)
#define MT_PCF85063_TIME_REG_COUNT     (7)

#define MT_PCF85063_CTRL1_EXT_TEST     (1U << 7)
#define MT_PCF85063_CTRL1_STOP         (1U << 5)
#define MT_PCF85063_CTRL1_12_24        (1U << 1)

#define MT_PCF85063_SECONDS_OS         (1U << 7)
#define MT_PCF85063_SECONDS_MASK       (0x7F)
#define MT_PCF85063_MINUTES_MASK       (0x7F)
#define MT_PCF85063_HOURS_MASK         (0x3F)
#define MT_PCF85063_DAYS_MASK          (0x3F)
#define MT_PCF85063_WEEKDAYS_MASK      (0x07)
#define MT_PCF85063_MONTHS_MASK        (0x1F)

struct mt_pcf85063
{
    i2c_master_dev_handle_t rtc_dev;
    SemaphoreHandle_t lock;
    bool initialized;
};

static bool _pcf85063_is_leap_year(int year)
{
    return (year % 4 == 0) &&
           ((year % 100 != 0) || (year % 400 == 0));
}

static int _pcf85063_days_in_month(int year, int month)
{
    static const uint8_t days[] =
    {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31,
    };
    int result = days[month - 1];
    if (month == 2 && _pcf85063_is_leap_year(year))
    {
        result = 29;
    }
    return result;
}

static int _pcf85063_day_of_year(int year, int month, int day)
{
    int result = day - 1;
    for (int current_month = 1; current_month < month; ++current_month)
    {
        result += _pcf85063_days_in_month(year, current_month);
    }
    return result;
}

static int _pcf85063_weekday(int year, int month, int day)
{
    static const int offsets[] =
    {
        0, 3, 2, 5, 0, 3,
        5, 1, 4, 6, 2, 4,
    };
    if (month < 3)
    {
        --year;
    }
    return (year + year / 4 - year / 100 + year / 400 +
            offsets[month - 1] + day) % 7;
}

static bool _pcf85063_bcd_decode(uint8_t raw, uint8_t allowed_mask,
                                 uint8_t minimum, uint8_t maximum,
                                 uint8_t *value)
{
    if (value == NULL || (raw & (uint8_t)~allowed_mask) != 0)
    {
        return false;
    }
    const uint8_t masked = raw & allowed_mask;
    const uint8_t high = masked >> 4;
    const uint8_t low = masked & 0x0FU;
    if (high > 9U || low > 9U)
    {
        return false;
    }
    const uint8_t decoded = (uint8_t)(high * 10U + low);
    if (decoded < minimum || decoded > maximum)
    {
        return false;
    }
    *value = decoded;
    return true;
}

static uint8_t _pcf85063_dec_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4) | (value % 10U));
}

static esp_err_t _pcf85063_read_registers(
    mt_pcf85063_t *device, uint8_t reg_addr, uint8_t *data, size_t len)
{
    if (!device || !device->rtc_dev || !data || len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_transmit_receive(device->rtc_dev,
                                       &reg_addr,
                                       sizeof(reg_addr),
                                       data,
                                       len,
                                       MT_PCF85063_I2C_TIMEOUT_MS);
}

static esp_err_t _pcf85063_write_registers(
    mt_pcf85063_t *device, uint8_t reg_addr,
    const uint8_t *data, size_t len)
{
    if (!device || !device->rtc_dev || !data || len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[1 + MT_PCF85063_TIME_REG_COUNT] = {0};
    if (len > (sizeof(buffer) - 1U))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[0] = reg_addr;
    memcpy(&buffer[1], data, len);

    return i2c_master_transmit(device->rtc_dev,
                               buffer,
                               len + 1U,
                               MT_PCF85063_I2C_TIMEOUT_MS);
}

static esp_err_t _pcf85063_read_control_1(mt_pcf85063_t *device, uint8_t *control_1)
{
    return _pcf85063_read_registers(device, MT_PCF85063_REG_CONTROL_1, control_1, 1);
}

static esp_err_t _pcf85063_write_control_1(mt_pcf85063_t *device, uint8_t control_1)
{
    return _pcf85063_write_registers(device, MT_PCF85063_REG_CONTROL_1, &control_1, 1);
}

static esp_err_t _pcf85063_normalize_mode(mt_pcf85063_t *device)
{
    uint8_t control_1 = 0;
    esp_err_t result = _pcf85063_read_control_1(device, &control_1);
    if (result != ESP_OK)
    {
        return result;
    }

    uint8_t normalized = (uint8_t)(control_1 &
                                   ~(MT_PCF85063_CTRL1_EXT_TEST |
                                     MT_PCF85063_CTRL1_12_24 |
                                     MT_PCF85063_CTRL1_STOP));
    if (normalized != control_1)
    {
        result = _pcf85063_write_control_1(device, normalized);
    }
    return result;
}

static esp_err_t _pcf85063_validate_time_for_set(
    const struct tm *timeinfo, struct tm *normalized_time)
{
    if (!timeinfo || !normalized_time)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (timeinfo->tm_year < 100 || timeinfo->tm_year > 199 ||
            timeinfo->tm_mon < 0 || timeinfo->tm_mon > 11 ||
            timeinfo->tm_hour < 0 || timeinfo->tm_hour > 23 ||
            timeinfo->tm_min < 0 || timeinfo->tm_min > 59 ||
            timeinfo->tm_sec < 0 || timeinfo->tm_sec > 59)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const int year = timeinfo->tm_year + 1900;
    const int month = timeinfo->tm_mon + 1;
    if (timeinfo->tm_mday < 1 ||
            timeinfo->tm_mday > _pcf85063_days_in_month(year, month))
    {
        return ESP_ERR_INVALID_ARG;
    }

    *normalized_time = *timeinfo;
    normalized_time->tm_wday = _pcf85063_weekday(
                                   year, month, timeinfo->tm_mday);
    normalized_time->tm_yday = _pcf85063_day_of_year(
                                   year, month, timeinfo->tm_mday);
    normalized_time->tm_isdst = 0;
    return ESP_OK;
}

esp_err_t mt_pcf85063_create(i2c_master_bus_handle_t i2c_bus, mt_pcf85063_t **out_device)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    mt_pcf85063_t *device = NULL;
    if (!i2c_bus || !out_device)
    {
        return result;
    }

    *out_device = NULL;

    device = calloc(1, sizeof(*device));
    if (!device)
    {
        return ESP_ERR_NO_MEM;
    }

    device->lock = xSemaphoreCreateMutex();
    if (!device->lock)
    {
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    const i2c_device_config_t rtc_cfg =
    {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MT_PCF85063_I2C_ADDR,
        .scl_speed_hz = MT_PCF85063_I2C_SPEED_HZ,
    };

    result = i2c_master_bus_add_device(i2c_bus, &rtc_cfg, &device->rtc_dev);
    if (result != ESP_OK)
    {
        goto cleanup;
    }

    result = _pcf85063_normalize_mode(device);
    if (result != ESP_OK)
    {
        esp_err_t cleanup_ret = mt_pcf85063_destroy(device);
        if (cleanup_ret != ESP_OK)
        {
            *out_device = device;
            result = cleanup_ret;
        }
        return result;
    }

    device->initialized = true;
    *out_device = device;
    return result;

cleanup:
    if (device->lock != NULL)
    {
        vSemaphoreDelete(device->lock);
    }
    free(device);
    return result;
}

esp_err_t mt_pcf85063_destroy(mt_pcf85063_t *device)
{
    if (!device)
    {
        return ESP_OK;
    }

    if (device->rtc_dev)
    {
        esp_err_t result = i2c_master_bus_rm_device(device->rtc_dev);
        if (result != ESP_OK)
        {
            return result;
        }
        device->rtc_dev = NULL;
    }

    if (device->lock)
    {
        vSemaphoreDelete(device->lock);
        device->lock = NULL;
    }

    free(device);
    return ESP_OK;
}

bool mt_pcf85063_is_ready(const mt_pcf85063_t *device)
{
    return device && device->initialized;
}

static esp_err_t _pcf85063_decode_time(
    const uint8_t raw_time[MT_PCF85063_TIME_REG_COUNT], struct tm *value)
{
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t weekday;
    uint8_t month;
    uint8_t year_offset;
    const bool valid =
        _pcf85063_bcd_decode(raw_time[0] & MT_PCF85063_SECONDS_MASK,
                             MT_PCF85063_SECONDS_MASK, 0, 59, &second) &&
        _pcf85063_bcd_decode(raw_time[1], MT_PCF85063_MINUTES_MASK,
                             0, 59, &minute) &&
        _pcf85063_bcd_decode(raw_time[2], MT_PCF85063_HOURS_MASK,
                             0, 23, &hour) &&
        _pcf85063_bcd_decode(raw_time[3], MT_PCF85063_DAYS_MASK,
                             1, 31, &day) &&
        _pcf85063_bcd_decode(raw_time[4], MT_PCF85063_WEEKDAYS_MASK,
                             0, 6, &weekday) &&
        _pcf85063_bcd_decode(raw_time[5], MT_PCF85063_MONTHS_MASK,
                             1, 12, &month) &&
        _pcf85063_bcd_decode(raw_time[6], UINT8_MAX,
                             0, 99, &year_offset);
    if (!valid)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const int year = 2000 + year_offset;
    if (day > _pcf85063_days_in_month(year, month) ||
            weekday != _pcf85063_weekday(year, month, day))
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *value = (struct tm)
    {
        .tm_sec = second,
        .tm_min = minute,
        .tm_hour = hour,
        .tm_mday = day,
        .tm_wday = weekday,
        .tm_mon = month - 1,
        .tm_year = year - 1900,
        .tm_yday = _pcf85063_day_of_year(year, month, day),
        .tm_isdst = 0,
    };
    return ESP_OK;
}

esp_err_t mt_pcf85063_get_time(mt_pcf85063_t *device, struct tm *timeinfo)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    bool lock_owned = false;
    if (!device || !timeinfo)
    {
        return result;
    }

    if (!device->initialized || !device->lock)
    {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(device->lock, portMAX_DELAY);
    lock_owned = true;

    uint8_t control_1 = 0;
    result = _pcf85063_read_control_1(device, &control_1);
    if (result != ESP_OK)
    {
        goto exit;
    }
    if ((control_1 & (MT_PCF85063_CTRL1_EXT_TEST |
                      MT_PCF85063_CTRL1_12_24 |
                      MT_PCF85063_CTRL1_STOP)) != 0)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    uint8_t raw_time[MT_PCF85063_TIME_REG_COUNT] = {0};
    result = _pcf85063_read_registers(device, MT_PCF85063_REG_SECONDS,
                                      raw_time, sizeof(raw_time));
    if (result != ESP_OK)
    {
        goto exit;
    }

    if ((raw_time[0] & MT_PCF85063_SECONDS_OS) != 0)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    struct tm value;
    result = _pcf85063_decode_time(raw_time, &value);
    if (result == ESP_OK)
    {
        *timeinfo = value;
    }

exit:
    if (lock_owned)
    {
        xSemaphoreGive(device->lock);
    }
    return result;
}

esp_err_t mt_pcf85063_set_time(mt_pcf85063_t *device, const struct tm *timeinfo)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    bool lock_owned = false;
    if (!device || !timeinfo)
    {
        return result;
    }

    if (!device->initialized || !device->lock)
    {
        return ESP_ERR_INVALID_STATE;
    }

    struct tm normalized = {0};
    result = _pcf85063_validate_time_for_set(timeinfo, &normalized);
    if (result != ESP_OK)
    {
        return result;
    }

    xSemaphoreTake(device->lock, portMAX_DELAY);
    lock_owned = true;

    uint8_t control_1 = 0;
    result = _pcf85063_read_control_1(device, &control_1);
    if (result != ESP_OK)
    {
        goto exit;
    }

    uint8_t stopped_control_1 = (uint8_t)(
                                    (control_1 & ~(MT_PCF85063_CTRL1_EXT_TEST |
                                        MT_PCF85063_CTRL1_12_24)) |
                                    MT_PCF85063_CTRL1_STOP);
    result = _pcf85063_write_control_1(device, stopped_control_1);
    if (result != ESP_OK)
    {
        goto exit;
    }

    const uint8_t raw_time[MT_PCF85063_TIME_REG_COUNT] =
    {
        _pcf85063_dec_to_bcd((uint8_t)normalized.tm_sec),
        _pcf85063_dec_to_bcd((uint8_t)normalized.tm_min),
        _pcf85063_dec_to_bcd((uint8_t)normalized.tm_hour),
        _pcf85063_dec_to_bcd((uint8_t)normalized.tm_mday),
        _pcf85063_dec_to_bcd((uint8_t)normalized.tm_wday),
        _pcf85063_dec_to_bcd((uint8_t)(normalized.tm_mon + 1)),
        _pcf85063_dec_to_bcd((uint8_t)(normalized.tm_year - 100)),
    };

    result = _pcf85063_write_registers(
                 device, MT_PCF85063_REG_SECONDS, raw_time, sizeof(raw_time));
    const uint8_t running_control_1 = (uint8_t)(stopped_control_1 & ~MT_PCF85063_CTRL1_STOP);
    esp_err_t resume_ret = _pcf85063_write_control_1(device, running_control_1);
    if (result == ESP_OK)
    {
        result = resume_ret;
    }

exit:
    if (lock_owned)
    {
        xSemaphoreGive(device->lock);
    }
    return result;
}

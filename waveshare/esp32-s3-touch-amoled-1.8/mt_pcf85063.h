#ifndef __MT_PCF85063_H__
#define __MT_PCF85063_H__

#include <stdbool.h>
#include <time.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque PCF85063 RTC instance. */
typedef struct mt_pcf85063 mt_pcf85063_t;

/** @brief Calendar fields compared by the PCF85063 recurring alarm. */
typedef struct mt_pcf85063_alarm_config
{
    bool match_second; /**< Compare second when true. */
    uint8_t second;    /**< Second in the range 0 through 59. */
    bool match_minute; /**< Compare minute when true. */
    uint8_t minute;    /**< Minute in the range 0 through 59. */
    bool match_hour;   /**< Compare hour when true. */
    uint8_t hour;      /**< Hour in the range 0 through 23. */
    bool match_day;    /**< Compare day of month when true. */
    uint8_t day;       /**< Day of month in the range 1 through 31. */
    bool match_weekday; /**< Compare weekday when true. */
    uint8_t weekday;    /**< Weekday in the range 0 through 6. */
} mt_pcf85063_alarm_config_t;

/** @brief PCF85063 alarm control and latched-flag state. */
typedef struct mt_pcf85063_alarm_status
{
    bool enabled; /**< Alarm interrupt generation is enabled. */
    bool pending; /**< Alarm flag is latched and awaiting a clear. */
} mt_pcf85063_alarm_status_t;

/**
 * @brief Create and normalize a PCF85063 RTC instance.
 *
 * @note When rollback fails, an error is returned and out_device receives the
 *       partial instance so mt_pcf85063_destroy() can be retried.
 *
 * @param i2c_bus is the initialized I2C master bus.
 * @param out_device receives the owned RTC instance.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t mt_pcf85063_create(i2c_master_bus_handle_t i2c_bus,
                             mt_pcf85063_t **out_device);

/**
 * @brief Remove the I2C device and release a PCF85063 instance.
 *
 * @param device is the instance to release; NULL is accepted.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t mt_pcf85063_destroy(mt_pcf85063_t *device);

/**
 * @brief Report whether a PCF85063 instance completed initialization.
 *
 * @param device is the RTC instance to inspect.
 *
 * @return true when ready; false otherwise.
 */
bool mt_pcf85063_is_ready(const mt_pcf85063_t *device);

/**
 * @brief Read and validate the current RTC calendar value.
 *
 * @param device is the initialized RTC instance.
 * @param timeinfo receives time only after all validation succeeds.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t mt_pcf85063_get_time(mt_pcf85063_t *device,
                               struct tm *timeinfo);

/**
 * @brief Validate and write an RTC calendar value.
 *
 * @param device is the initialized RTC instance.
 * @param timeinfo is the local calendar value in years 2000 through 2099.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t mt_pcf85063_set_time(mt_pcf85063_t *device,
                               const struct tm *timeinfo);

/**
 * @brief Configure and enable the recurring calendar alarm.
 *
 * @note At least one comparison field must be enabled. The alarm interrupt is
 *       disabled and its pending flag is cleared before registers are changed.
 *
 * @param device is the initialized RTC instance.
 * @param config selects the calendar fields and comparison values.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t mt_pcf85063_alarm_configure(
    mt_pcf85063_t *device, const mt_pcf85063_alarm_config_t *config);

/**
 * @brief Disable the alarm interrupt and clear its pending flag.
 *
 * @param device is the initialized RTC instance.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t mt_pcf85063_alarm_disable(mt_pcf85063_t *device);

/**
 * @brief Read alarm interrupt enable and pending state.
 *
 * @param device is the initialized RTC instance.
 * @param status receives state only after a successful register read.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t mt_pcf85063_alarm_get_status(
    mt_pcf85063_t *device, mt_pcf85063_alarm_status_t *status);

/**
 * @brief Clear the latched alarm flag without changing alarm enable state.
 *
 * @param device is the initialized RTC instance.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t mt_pcf85063_alarm_clear(mt_pcf85063_t *device);

#ifdef __cplusplus
}
#endif

#endif /* __MT_PCF85063_H__ */

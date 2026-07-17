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

#ifdef __cplusplus
}
#endif

#endif /* __MT_PCF85063_H__ */

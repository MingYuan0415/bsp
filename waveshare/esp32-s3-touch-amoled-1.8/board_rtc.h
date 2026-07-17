#ifndef __BOARD_RTC_H__
#define __BOARD_RTC_H__

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the PCF85063 RTC and register its BSP operations.
 *
 * @param i2c_bus is the initialized board I2C bus.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_rtc_init(i2c_master_bus_handle_t i2c_bus);

/**
 * @brief Release the PCF85063 RTC instance.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_rtc_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_RTC_H__ */

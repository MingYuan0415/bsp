#ifndef __BOARD_RTC_H__
#define __BOARD_RTC_H__

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#include "board_tca9554.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the PCF85063 RTC and register its BSP operations.
 *
 * @param i2c_bus is the initialized board I2C bus.
 * @param io_expander is the TCA9554 carrying RTC_INT.
 * @param alarm_pin_mask selects the single active-low RTC_INT input.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_rtc_init(i2c_master_bus_handle_t i2c_bus,
                         board_tca9554_t *io_expander,
                         uint8_t alarm_pin_mask);

/**
 * @brief Release the PCF85063 RTC instance.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_rtc_deinit(void);

/** @brief Report whether RTC cleanup still owns board resources. */
bool board_rtc_has_resources(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_RTC_H__ */

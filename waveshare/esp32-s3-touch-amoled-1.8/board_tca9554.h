#ifndef __BOARD_TCA9554_H__
#define __BOARD_TCA9554_H__

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_io_expander.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque TCA9554 device instance. */
typedef struct board_tca9554 board_tca9554_t;

/**
 * @brief Create and reset a TCA9554 I/O expander.
 *
 * @note When rollback fails, an error is returned and out_device receives the
 *       partial instance so board_tca9554_destroy() can be retried.
 *
 * @param i2c_bus is the initialized I2C master bus.
 * @param device_address is the 7-bit TCA9554 address.
 * @param out_device receives the owned device instance.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_tca9554_create(i2c_master_bus_handle_t i2c_bus,
                               uint8_t device_address,
                               board_tca9554_t **out_device);

/**
 * @brief Remove the I2C device and release a TCA9554 instance.
 *
 * @param device is the instance to release; NULL is accepted.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_tca9554_destroy(board_tca9554_t *device);

/**
 * @brief Return the ESP I/O-expander base handle.
 *
 * @param device is the TCA9554 instance.
 *
 * @return Base handle, or NULL when device is NULL.
 */
esp_io_expander_handle_t board_tca9554_get_expander(
    board_tca9554_t *device);

/**
 * @brief Read the current TCA9554 input register.
 *
 * @param device is the initialized TCA9554 instance.
 * @param value receives all eight input levels.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_tca9554_read_inputs(board_tca9554_t *device,
                                    uint8_t *value);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_TCA9554_H__ */

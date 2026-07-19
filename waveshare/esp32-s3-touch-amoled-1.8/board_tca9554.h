#ifndef __BOARD_TCA9554_H__
#define __BOARD_TCA9554_H__

#include <stdbool.h>
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

/**
 * @brief Set one or more already-configured output pins atomically.
 *
 * @param device is the initialized TCA9554 instance.
 * @param pin_mask selects output pins (bit 0 maps to P0).
 * @param level is the level to apply to every selected pin.
 *
 * @return ESP_OK on success; ESP_ERR_INVALID_STATE when a selected pin is an
 *         input; otherwise an ESP-IDF error.
 */
esp_err_t board_tca9554_set_output_level(board_tca9554_t *device,
        uint8_t pin_mask,
        uint8_t level);

/**
 * @brief Configure the direction of one or more pins atomically.
 *
 * @param device is the initialized TCA9554 instance.
 * @param pin_mask selects pins (bit 0 maps to P0).
 * @param output selects output (true) or input (false).
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_tca9554_set_direction(board_tca9554_t *device,
                                      uint8_t pin_mask,
                                      bool output);

/**
 * @brief Read selected input levels from the TCA9554.
 *
 * @param device is the initialized TCA9554 instance.
 * @param pin_mask selects pins (bit 0 maps to P0).
 * @param level_mask receives the selected levels.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_tca9554_get_input_level(board_tca9554_t *device,
                                        uint8_t pin_mask,
                                        uint8_t *level_mask);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_TCA9554_H__ */

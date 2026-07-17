#ifndef __BOARD_I2C_PANEL_IO_H__
#define __BOARD_I2C_PANEL_IO_H__

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create an ESP LCD panel-I/O adapter for an I2C device.
 *
 * @param bus is the initialized I2C master bus.
 * @param device_address is the 7-bit I2C device address.
 * @param clock_speed_hz is the device bus clock in hertz.
 * @param out_io receives the owned panel-I/O handle.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_i2c_panel_io_create(i2c_master_bus_handle_t bus,
                                    uint8_t device_address,
                                    uint32_t clock_speed_hz,
                                    esp_lcd_panel_io_handle_t *out_io);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_I2C_PANEL_IO_H__ */

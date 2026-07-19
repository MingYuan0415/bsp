#ifndef __ESP_LCD_PANEL_IO_H__
#define __ESP_LCD_PANEL_IO_H__

#include <stddef.h>
#include <stdint.h>

#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_types.h"

typedef struct esp_lcd_panel_io_spi_config
{
    uint32_t pclk_hz;
    int trans_queue_depth;
} esp_lcd_panel_io_spi_config_t;

typedef struct esp_lcd_panel_io_i2c_config
{
    int unused;
} esp_lcd_panel_io_i2c_config_t;

esp_err_t esp_lcd_new_panel_io_spi(
    spi_host_device_t host,
    const esp_lcd_panel_io_spi_config_t *config,
    esp_lcd_panel_io_handle_t *out_io);
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io,
                                    int command,
                                    const void *param,
                                    size_t param_size);
esp_err_t esp_lcd_panel_io_rx_param(esp_lcd_panel_io_handle_t io,
                                    int command,
                                    void *param,
                                    size_t param_size);
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io);

#endif /* __ESP_LCD_PANEL_IO_H__ */

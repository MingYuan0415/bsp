#ifndef __ESP_LCD_SH8601_H__
#define __ESP_LCD_SH8601_H__

#include <stddef.h>
#include <stdint.h>

#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

typedef struct sh8601_lcd_init_cmd
{
    uint8_t cmd;
    const void *data;
    size_t data_bytes;
    unsigned int delay_ms;
} sh8601_lcd_init_cmd_t;

typedef struct sh8601_vendor_config
{
    const sh8601_lcd_init_cmd_t *init_cmds;
    size_t init_cmds_size;
    struct
    {
        unsigned int use_qspi_interface : 1;
    } flags;
} sh8601_vendor_config_t;

#define SH8601_PANEL_BUS_QSPI_CONFIG(pclk, data0, data1, data2, data3, size) \
    ((spi_bus_config_t){0})
#define SH8601_PANEL_IO_QSPI_CONFIG(cs, callback, context) \
    ((esp_lcd_panel_io_spi_config_t){0})

esp_err_t esp_lcd_new_panel_sh8601(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_dev_config_t *config,
    esp_lcd_panel_handle_t *out_panel);

#endif /* __ESP_LCD_SH8601_H__ */

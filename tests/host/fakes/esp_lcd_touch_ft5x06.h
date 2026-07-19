#ifndef __ESP_LCD_TOUCH_FT5X06_H__
#define __ESP_LCD_TOUCH_FT5X06_H__

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"

typedef struct esp_lcd_touch_config
{
    int x_max;
    int y_max;
    int rst_gpio_num;
    int int_gpio_num;
    struct
    {
        int reset;
        int interrupt;
    } levels;
    struct
    {
        unsigned int swap_xy : 1;
        unsigned int mirror_x : 1;
        unsigned int mirror_y : 1;
    } flags;
} esp_lcd_touch_config_t;

#define ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG() \
    ((esp_lcd_panel_io_i2c_config_t){0})
#define ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS (0x38U)

esp_err_t esp_lcd_touch_new_i2c_ft5x06(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_touch_config_t *config,
    esp_lcd_touch_handle_t *out_touch);
esp_err_t esp_lcd_touch_del(esp_lcd_touch_handle_t touch);

#endif /* __ESP_LCD_TOUCH_FT5X06_H__ */

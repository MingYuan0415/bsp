#ifndef __ESP_LCD_PANEL_OPS_H__
#define __ESP_LCD_PANEL_OPS_H__

#include <stdbool.h>

#include "esp_err.h"
#include "esp_lcd_types.h"

typedef enum
{
    LCD_RGB_ELEMENT_ORDER_RGB = 0,
} lcd_rgb_element_order_t;

typedef struct esp_lcd_panel_dev_config
{
    int reset_gpio_num;
    lcd_rgb_element_order_t rgb_ele_order;
    int bits_per_pixel;
    void *vendor_config;
} esp_lcd_panel_dev_config_t;

esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t panel);
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t panel);
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t panel, bool on);
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t panel);

#endif /* __ESP_LCD_PANEL_OPS_H__ */

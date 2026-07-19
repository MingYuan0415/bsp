#ifndef __SDSPI_FAKE_GPIO_WRAPPER_H__
#define __SDSPI_FAKE_GPIO_WRAPPER_H__

#include <stdint.h>

#include "esp_err.h"
#include "esp_io_expander.h"

esp_err_t esp_io_expander_gpio_wrapper_append_handler(
    esp_io_expander_handle_t handler, uint32_t start_io_num);
esp_err_t esp_io_expander_gpio_wrapper_remove_handler(
    esp_io_expander_handle_t handler);

#endif /* __SDSPI_FAKE_GPIO_WRAPPER_H__ */

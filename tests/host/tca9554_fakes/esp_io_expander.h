#ifndef __TCA9554_FAKE_ESP_IO_EXPANDER_H__
#define __TCA9554_FAKE_ESP_IO_EXPANDER_H__

#include <stdint.h>

#include "esp_err.h"

typedef struct esp_io_expander_s esp_io_expander_t;
typedef esp_io_expander_t *esp_io_expander_handle_t;

typedef struct esp_io_expander_config
{
    uint8_t io_count;
    struct
    {
        uint8_t dir_out_bit_zero : 1;
    } flags;
} esp_io_expander_config_t;

struct esp_io_expander_s
{
    esp_err_t (*read_input_reg)(esp_io_expander_handle_t handle,
                                uint32_t *value);
    esp_err_t (*write_output_reg)(esp_io_expander_handle_t handle,
                                  uint32_t value);
    esp_err_t (*read_output_reg)(esp_io_expander_handle_t handle,
                                 uint32_t *value);
    esp_err_t (*write_direction_reg)(esp_io_expander_handle_t handle,
                                     uint32_t value);
    esp_err_t (*read_direction_reg)(esp_io_expander_handle_t handle,
                                    uint32_t *value);
    esp_err_t (*reset)(esp_io_expander_handle_t handle);
    esp_err_t (*del)(esp_io_expander_handle_t handle);
    esp_io_expander_config_t config;
};

#endif /* __TCA9554_FAKE_ESP_IO_EXPANDER_H__ */

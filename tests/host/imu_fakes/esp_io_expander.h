#ifndef __IMU_FAKE_ESP_IO_EXPANDER_H__
#define __IMU_FAKE_ESP_IO_EXPANDER_H__

#include <stdint.h>

#include "esp_err.h"

typedef struct fake_io_expander *esp_io_expander_handle_t;

#define IO_EXPANDER_PIN_NUM_6 (1U << 6)

typedef enum
{
    IO_EXPANDER_INPUT = 0,
    IO_EXPANDER_OUTPUT,
} esp_io_expander_dir_t;

esp_err_t esp_io_expander_set_dir(esp_io_expander_handle_t handle,
                                  uint32_t pin_mask,
                                  esp_io_expander_dir_t direction);
esp_err_t esp_io_expander_get_level(esp_io_expander_handle_t handle,
                                    uint32_t pin_mask,
                                    uint32_t *level_mask);

#endif /* __IMU_FAKE_ESP_IO_EXPANDER_H__ */

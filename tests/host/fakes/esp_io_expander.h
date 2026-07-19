#ifndef __ESP_IO_EXPANDER_H__
#define __ESP_IO_EXPANDER_H__

#include <stdint.h>

#include "esp_err.h"

/** @brief Opaque host-test I/O-expander handle. */
typedef void *esp_io_expander_handle_t;

#define IO_EXPANDER_PIN_NUM_0 (1U << 0)
#define IO_EXPANDER_PIN_NUM_1 (1U << 1)
#define IO_EXPANDER_PIN_NUM_2 (1U << 2)
#define IO_EXPANDER_PIN_NUM_4 (1U << 4)

esp_err_t esp_io_expander_set_level(esp_io_expander_handle_t handle,
                                    uint32_t pin, uint8_t level);

#endif /* __ESP_IO_EXPANDER_H__ */

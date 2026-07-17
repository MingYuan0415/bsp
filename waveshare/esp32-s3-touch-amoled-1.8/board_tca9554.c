#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "board_tca9554.h"

#define BOARD_TCA9554_IO_COUNT          (8U)
#define BOARD_TCA9554_I2C_SPEED_HZ      (400000U)
#define BOARD_TCA9554_I2C_TIMEOUT_MS    (20)

#define BOARD_TCA9554_REG_INPUT         (0x00U)
#define BOARD_TCA9554_REG_OUTPUT        (0x01U)
#define BOARD_TCA9554_REG_DIRECTION     (0x03U)
#define BOARD_TCA9554_REGISTER_DEFAULT  (0xFFU)

struct board_tca9554
{
    esp_io_expander_t base;
    i2c_master_dev_handle_t i2c_device;
    SemaphoreHandle_t lock;
    uint8_t output;
    uint8_t direction;
};

static board_tca9554_t *_board_tca9554_from_base(esp_io_expander_handle_t handle)
{
    return (board_tca9554_t *)((uint8_t *)handle - offsetof(board_tca9554_t, base));
}

static esp_err_t _board_tca9554_read_register(board_tca9554_t *device,
        uint8_t address,
        uint8_t *value)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (device == NULL || device->i2c_device == NULL || value == NULL)
    {
        goto exit;
    }

    xSemaphoreTake(device->lock, portMAX_DELAY);
    result = i2c_master_transmit_receive(device->i2c_device,
                                         &address,
                                         sizeof(address),
                                         value,
                                         sizeof(*value),
                                         BOARD_TCA9554_I2C_TIMEOUT_MS);
    xSemaphoreGive(device->lock);

exit:
    return result;
}

static esp_err_t _board_tca9554_write_register(board_tca9554_t *device,
        uint8_t address,
        uint8_t value)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (device == NULL || device->i2c_device == NULL)
    {
        goto exit;
    }

    const uint8_t data[] = {address, value};
    xSemaphoreTake(device->lock, portMAX_DELAY);
    result = i2c_master_transmit(device->i2c_device,
                                 data,
                                 sizeof(data),
                                 BOARD_TCA9554_I2C_TIMEOUT_MS);
    xSemaphoreGive(device->lock);

exit:
    return result;
}

esp_err_t board_tca9554_read_inputs(board_tca9554_t *device, uint8_t *value)
{
    return _board_tca9554_read_register(device, BOARD_TCA9554_REG_INPUT, value);
}

static esp_err_t _board_tca9554_read_input_reg(esp_io_expander_handle_t handle, uint32_t *value)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (handle == NULL || value == NULL)
    {
        goto exit;
    }

    uint8_t raw = 0;
    result = board_tca9554_read_inputs(_board_tca9554_from_base(handle), &raw);
    if (result == ESP_OK)
    {
        *value = raw;
    }

exit:
    return result;
}

static esp_err_t _board_tca9554_write_output_reg(esp_io_expander_handle_t handle, uint32_t value)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (handle == NULL)
    {
        goto exit;
    }

    board_tca9554_t *device = _board_tca9554_from_base(handle);
    const uint8_t output = (uint8_t)value;
    result = _board_tca9554_write_register(device, BOARD_TCA9554_REG_OUTPUT, output);
    if (result == ESP_OK)
    {
        device->output = output;
    }

exit:
    return result;
}

static esp_err_t _board_tca9554_read_output_reg(esp_io_expander_handle_t handle, uint32_t *value)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (handle == NULL || value == NULL)
    {
        goto exit;
    }

    *value = _board_tca9554_from_base(handle)->output;
    result = ESP_OK;

exit:
    return result;
}

static esp_err_t _board_tca9554_write_direction_reg(esp_io_expander_handle_t handle, uint32_t value)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (handle == NULL)
    {
        goto exit;
    }

    board_tca9554_t *device = _board_tca9554_from_base(handle);
    const uint8_t direction = (uint8_t)value;
    result = _board_tca9554_write_register(
                 device, BOARD_TCA9554_REG_DIRECTION, direction);
    if (result == ESP_OK)
    {
        device->direction = direction;
    }

exit:
    return result;
}

static esp_err_t _board_tca9554_read_direction_reg(esp_io_expander_handle_t handle, uint32_t *value)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (handle == NULL || value == NULL)
    {
        goto exit;
    }

    *value = _board_tca9554_from_base(handle)->direction;
    result = ESP_OK;

exit:
    return result;
}

static esp_err_t _board_tca9554_reset(esp_io_expander_handle_t handle)
{
    esp_err_t result = _board_tca9554_write_direction_reg(
                           handle, BOARD_TCA9554_REGISTER_DEFAULT);
    if (result == ESP_OK)
    {
        result = _board_tca9554_write_output_reg(
                     handle, BOARD_TCA9554_REGISTER_DEFAULT);
    }
    return result;
}

static esp_err_t _board_tca9554_delete_base(esp_io_expander_handle_t handle)
{
    return board_tca9554_destroy(_board_tca9554_from_base(handle));
}

esp_err_t board_tca9554_create(i2c_master_bus_handle_t i2c_bus,
                               uint8_t device_address,
                               board_tca9554_t **out_device)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    board_tca9554_t *device = NULL;
    if (i2c_bus == NULL || out_device == NULL)
    {
        goto exit;
    }
    *out_device = NULL;

    device = calloc(1, sizeof(*device));
    if (device == NULL)
    {
        result = ESP_ERR_NO_MEM;
        goto exit;
    }

    device->lock = xSemaphoreCreateMutex();
    if (device->lock == NULL)
    {
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    const i2c_device_config_t config =
    {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_address,
        .scl_speed_hz = BOARD_TCA9554_I2C_SPEED_HZ,
    };
    result = i2c_master_bus_add_device(i2c_bus, &config, &device->i2c_device);
    if (result != ESP_OK)
    {
        goto cleanup;
    }

    device->base.read_input_reg = _board_tca9554_read_input_reg;
    device->base.write_output_reg = _board_tca9554_write_output_reg;
    device->base.read_output_reg = _board_tca9554_read_output_reg;
    device->base.write_direction_reg = _board_tca9554_write_direction_reg;
    device->base.read_direction_reg = _board_tca9554_read_direction_reg;
    device->base.reset = _board_tca9554_reset;
    device->base.del = _board_tca9554_delete_base;
    device->base.config.io_count = BOARD_TCA9554_IO_COUNT;
    device->base.config.flags.dir_out_bit_zero = 1;
    device->output = BOARD_TCA9554_REGISTER_DEFAULT;
    device->direction = BOARD_TCA9554_REGISTER_DEFAULT;

    result = _board_tca9554_reset(&device->base);
    if (result != ESP_OK)
    {
        esp_err_t cleanup_ret =
            i2c_master_bus_rm_device(device->i2c_device);
        if (cleanup_ret != ESP_OK)
        {
            *out_device = device;
            result = cleanup_ret;
            goto exit;
        }
        device->i2c_device = NULL;
        goto cleanup;
    }

    *out_device = device;
    goto exit;

cleanup:
    if (device->lock != NULL)
    {
        vSemaphoreDelete(device->lock);
    }
    free(device);

exit:
    return result;
}

esp_err_t board_tca9554_destroy(board_tca9554_t *device)
{
    esp_err_t result = ESP_OK;
    if (device == NULL)
    {
        goto exit;
    }

    if (device->i2c_device != NULL)
    {
        result = i2c_master_bus_rm_device(device->i2c_device);
        if (result != ESP_OK)
        {
            goto exit;
        }
        device->i2c_device = NULL;
    }

    vSemaphoreDelete(device->lock);
    device->lock = NULL;
    free(device);

exit:
    return result;
}

esp_io_expander_handle_t board_tca9554_get_expander(board_tca9554_t *device)
{
    return (device == NULL) ? NULL : &device->base;
}

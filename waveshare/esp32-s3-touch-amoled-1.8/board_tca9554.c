#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "board_tca9554.h"

#define DBG_TAG "board_tca9554"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#define BOARD_TCA9554_IO_COUNT          (8U)
#define BOARD_TCA9554_I2C_SPEED_HZ      (400000U)
#define BOARD_TCA9554_I2C_TIMEOUT_MS    (20)
#define BOARD_TCA9554_INIT_SETTLE_MS    (10U)
#define BOARD_TCA9554_INIT_ATTEMPTS     (10U)
#define BOARD_TCA9554_INIT_RETRY_MS     (10U)

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
        return result;
    }

    xSemaphoreTake(device->lock, portMAX_DELAY);
    result = i2c_master_transmit_receive(device->i2c_device,
                                         &address,
                                         sizeof(address),
                                         value,
                                         sizeof(*value),
                                         BOARD_TCA9554_I2C_TIMEOUT_MS);
    xSemaphoreGive(device->lock);

    return result;
}

static esp_err_t _board_tca9554_write_register(board_tca9554_t *device,
        uint8_t address,
        uint8_t value)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (device == NULL || device->i2c_device == NULL)
    {
        return result;
    }

    const uint8_t data[] = {address, value};
    xSemaphoreTake(device->lock, portMAX_DELAY);
    result = i2c_master_transmit(device->i2c_device,
                                 data,
                                 sizeof(data),
                                 BOARD_TCA9554_I2C_TIMEOUT_MS);
    xSemaphoreGive(device->lock);

    return result;
}

esp_err_t board_tca9554_read_inputs(board_tca9554_t *device, uint8_t *value)
{
    return _board_tca9554_read_register(device, BOARD_TCA9554_REG_INPUT, value);
}

esp_err_t board_tca9554_set_output_level(board_tca9554_t *device,
        uint8_t pin_mask,
        uint8_t level)
{
    if (device == NULL || device->i2c_device == NULL || device->lock == NULL ||
            pin_mask == 0U || (level != 0U && level != 1U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(device->lock, portMAX_DELAY);
    if ((device->direction & pin_mask) != 0U)
    {
        xSemaphoreGive(device->lock);
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t next_output = level != 0U ?
                                (uint8_t)(device->output | pin_mask) :
                                (uint8_t)(device->output & (uint8_t)~pin_mask);
    esp_err_t result = ESP_OK;
    if (next_output != device->output)
    {
        const uint8_t data[] = {BOARD_TCA9554_REG_OUTPUT, next_output};
        result = i2c_master_transmit(device->i2c_device,
                                     data,
                                     sizeof(data),
                                     BOARD_TCA9554_I2C_TIMEOUT_MS);
        if (result == ESP_OK)
        {
            device->output = next_output;
        }
    }
    xSemaphoreGive(device->lock);
    return result;
}

esp_err_t board_tca9554_set_direction(board_tca9554_t *device,
                                      uint8_t pin_mask,
                                      bool output)
{
    if (device == NULL || device->i2c_device == NULL || device->lock == NULL ||
            pin_mask == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(device->lock, portMAX_DELAY);
    const uint8_t next_direction = output ?
                                   (uint8_t)(device->direction & (uint8_t)~pin_mask) :
                                   (uint8_t)(device->direction | pin_mask);
    esp_err_t result = ESP_OK;
    if (next_direction != device->direction)
    {
        const uint8_t data[] = {BOARD_TCA9554_REG_DIRECTION, next_direction};
        result = i2c_master_transmit(device->i2c_device,
                                     data,
                                     sizeof(data),
                                     BOARD_TCA9554_I2C_TIMEOUT_MS);
        if (result == ESP_OK)
        {
            device->direction = next_direction;
        }
    }
    xSemaphoreGive(device->lock);
    return result;
}

esp_err_t board_tca9554_get_input_level(board_tca9554_t *device,
                                        uint8_t pin_mask,
                                        uint8_t *level_mask)
{
    if (device == NULL || level_mask == NULL || pin_mask == 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t input = 0U;
    esp_err_t result = board_tca9554_read_inputs(device, &input);
    if (result == ESP_OK)
    {
        *level_mask = (uint8_t)(input & pin_mask);
    }
    return result;
}

static esp_err_t _board_tca9554_read_input_reg(esp_io_expander_handle_t handle, uint32_t *value)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (handle == NULL || value == NULL)
    {
        return result;
    }

    uint8_t raw = 0;
    result = board_tca9554_read_inputs(_board_tca9554_from_base(handle), &raw);
    if (result == ESP_OK)
    {
        *value = raw;
    }

    return result;
}

static esp_err_t _board_tca9554_write_output_reg(esp_io_expander_handle_t handle, uint32_t value)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (handle == NULL)
    {
        return result;
    }

    board_tca9554_t *device = _board_tca9554_from_base(handle);
    const uint8_t output = (uint8_t)value;
    result = _board_tca9554_write_register(device, BOARD_TCA9554_REG_OUTPUT, output);
    if (result == ESP_OK)
    {
        device->output = output;
    }

    return result;
}

static esp_err_t _board_tca9554_read_output_reg(esp_io_expander_handle_t handle, uint32_t *value)
{
    if (handle == NULL || value == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    *value = _board_tca9554_from_base(handle)->output;
    return ESP_OK;
}

static esp_err_t _board_tca9554_write_direction_reg(esp_io_expander_handle_t handle, uint32_t value)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (handle == NULL)
    {
        return result;
    }

    board_tca9554_t *device = _board_tca9554_from_base(handle);
    const uint8_t direction = (uint8_t)value;
    result = _board_tca9554_write_register(
                 device, BOARD_TCA9554_REG_DIRECTION, direction);
    if (result == ESP_OK)
    {
        device->direction = direction;
    }

    return result;
}

static esp_err_t _board_tca9554_read_direction_reg(esp_io_expander_handle_t handle, uint32_t *value)
{
    if (handle == NULL || value == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    *value = _board_tca9554_from_base(handle)->direction;
    return ESP_OK;
}

static esp_err_t _board_tca9554_reset_once(
    esp_io_expander_handle_t handle, uint8_t *failed_register)
{
    if (failed_register != NULL)
    {
        *failed_register = BOARD_TCA9554_REG_DIRECTION;
    }
    esp_err_t result = _board_tca9554_write_direction_reg(
                           handle, BOARD_TCA9554_REGISTER_DEFAULT);
    if (result == ESP_OK)
    {
        if (failed_register != NULL)
        {
            *failed_register = BOARD_TCA9554_REG_OUTPUT;
        }
        result = _board_tca9554_write_output_reg(
                     handle, BOARD_TCA9554_REGISTER_DEFAULT);
    }
    return result;
}

static esp_err_t _board_tca9554_reset(esp_io_expander_handle_t handle)
{
    return _board_tca9554_reset_once(handle, NULL);
}

static bool _board_tca9554_is_transient_init_error(esp_err_t error)
{
    return error == ESP_ERR_INVALID_RESPONSE || error == ESP_ERR_TIMEOUT;
}

static esp_err_t _board_tca9554_initialize(
    esp_io_expander_handle_t handle, uint8_t *failed_register,
    uint8_t *attempt_count)
{
    esp_err_t result = ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(BOARD_TCA9554_INIT_SETTLE_MS));

    for (uint8_t attempt = 0U;
            attempt < BOARD_TCA9554_INIT_ATTEMPTS; ++attempt)
    {
        *attempt_count = attempt + 1U;
        result = _board_tca9554_reset_once(handle, failed_register);
        if (result == ESP_OK ||
                !_board_tca9554_is_transient_init_error(result))
        {
            break;
        }
        if (attempt + 1U < BOARD_TCA9554_INIT_ATTEMPTS)
        {
            vTaskDelay(pdMS_TO_TICKS(BOARD_TCA9554_INIT_RETRY_MS));
        }
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
        return result;
    }
    *out_device = NULL;

    device = calloc(1, sizeof(*device));
    if (device == NULL)
    {
        return ESP_ERR_NO_MEM;
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

    uint8_t failed_register = BOARD_TCA9554_REG_DIRECTION;
    uint8_t attempt_count = 0U;
    result = _board_tca9554_initialize(
                 &device->base, &failed_register, &attempt_count);
    if (result != ESP_OK)
    {
        LOG_E("init failed: addr=0x%02x reg=0x%02x error=%s attempts=%u",
              (unsigned int)device_address, (unsigned int)failed_register,
              esp_err_to_name(result), (unsigned int)attempt_count);
        esp_err_t cleanup_ret =
            i2c_master_bus_rm_device(device->i2c_device);
        if (cleanup_ret != ESP_OK)
        {
            *out_device = device;
            return cleanup_ret;
        }
        device->i2c_device = NULL;
        goto cleanup;
    }
    if (attempt_count > 1U)
    {
        LOG_W("init recovered: addr=0x%02x attempts=%u",
              (unsigned int)device_address, (unsigned int)attempt_count);
    }

    *out_device = device;
    return result;

cleanup:
    if (device->lock != NULL)
    {
        vSemaphoreDelete(device->lock);
    }
    free(device);
    return result;
}

esp_err_t board_tca9554_destroy(board_tca9554_t *device)
{
    if (device == NULL)
    {
        return ESP_OK;
    }

    if (device->i2c_device != NULL)
    {
        esp_err_t result = i2c_master_bus_rm_device(device->i2c_device);
        if (result != ESP_OK)
        {
            return result;
        }
        device->i2c_device = NULL;
    }

    vSemaphoreDelete(device->lock);
    device->lock = NULL;
    free(device);
    return ESP_OK;
}

esp_io_expander_handle_t board_tca9554_get_expander(board_tca9554_t *device)
{
    return (device == NULL) ? NULL : &device->base;
}

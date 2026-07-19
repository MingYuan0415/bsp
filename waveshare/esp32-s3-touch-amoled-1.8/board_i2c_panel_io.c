#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_interface.h"

#include "board_i2c_panel_io.h"

#define BOARD_I2C_PANEL_IO_TIMEOUT_MS       (20)
#define BOARD_I2C_PANEL_IO_MAX_PARAM_SIZE   (32U)

typedef struct board_i2c_panel_io
{
    esp_lcd_panel_io_t base;
    i2c_master_dev_handle_t device;
    SemaphoreHandle_t lock;
} board_i2c_panel_io_t;

static board_i2c_panel_io_t *_board_i2c_panel_io_from_base(esp_lcd_panel_io_t *base)
{
    return (board_i2c_panel_io_t *)((uint8_t *)base - offsetof(board_i2c_panel_io_t, base));
}

static esp_err_t _board_i2c_panel_io_rx(esp_lcd_panel_io_t *base,
                                        int command,
                                        void *parameters,
                                        size_t parameter_size)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (base == NULL || parameters == NULL || parameter_size == 0 ||
            command < 0 || command > UINT8_MAX)
    {
        return result;
    }

    board_i2c_panel_io_t *io = _board_i2c_panel_io_from_base(base);
    const uint8_t command_byte = (uint8_t)command;
    xSemaphoreTake(io->lock, portMAX_DELAY);
    result = i2c_master_transmit_receive(io->device,
                                         &command_byte,
                                         sizeof(command_byte),
                                         parameters,
                                         parameter_size,
                                         BOARD_I2C_PANEL_IO_TIMEOUT_MS);
    xSemaphoreGive(io->lock);

    return result;
}

static esp_err_t _board_i2c_panel_io_tx(esp_lcd_panel_io_t *base,
                                        int command,
                                        const void *parameters,
                                        size_t parameter_size)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (base == NULL || command < 0 || command > UINT8_MAX ||
            parameter_size > BOARD_I2C_PANEL_IO_MAX_PARAM_SIZE ||
            (parameter_size > 0 && parameters == NULL))
    {
        return result;
    }

    board_i2c_panel_io_t *io = _board_i2c_panel_io_from_base(base);
    uint8_t data[1U + BOARD_I2C_PANEL_IO_MAX_PARAM_SIZE] = {0};
    data[0] = (uint8_t)command;
    if (parameter_size > 0)
    {
        memcpy(&data[1], parameters, parameter_size);
    }

    xSemaphoreTake(io->lock, portMAX_DELAY);
    result = i2c_master_transmit(io->device,
                                 data,
                                 parameter_size + 1U,
                                 BOARD_I2C_PANEL_IO_TIMEOUT_MS);
    xSemaphoreGive(io->lock);

    return result;
}

static esp_err_t _board_i2c_panel_io_tx_color(esp_lcd_panel_io_t *base,
        int command,
        const void *color,
        size_t color_size)
{
    (void)base;
    (void)command;
    (void)color;
    (void)color_size;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t _board_i2c_panel_io_register_callbacks(esp_lcd_panel_io_t *base,
        const esp_lcd_panel_io_callbacks_t *callbacks,
        void *user_context)
{
    (void)base;
    (void)callbacks;
    (void)user_context;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t _board_i2c_panel_io_delete(esp_lcd_panel_io_t *base)
{
    if (base == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    board_i2c_panel_io_t *io = _board_i2c_panel_io_from_base(base);
    esp_err_t result = i2c_master_bus_rm_device(io->device);
    if (result != ESP_OK)
    {
        return result;
    }

    vSemaphoreDelete(io->lock);
    free(io);
    return result;
}

esp_err_t board_i2c_panel_io_create(i2c_master_bus_handle_t bus,
                                    uint8_t device_address,
                                    uint32_t clock_speed_hz,
                                    esp_lcd_panel_io_handle_t *out_io)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    board_i2c_panel_io_t *io = NULL;
    if (bus == NULL || clock_speed_hz == 0 || out_io == NULL)
    {
        return result;
    }
    *out_io = NULL;

    io = calloc(1, sizeof(*io));
    if (io == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    io->lock = xSemaphoreCreateMutex();
    if (io->lock == NULL)
    {
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    const i2c_device_config_t config =
    {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_address,
        .scl_speed_hz = clock_speed_hz,
    };
    result = i2c_master_bus_add_device(bus, &config, &io->device);
    if (result != ESP_OK)
    {
        goto cleanup;
    }

    io->base.rx_param = _board_i2c_panel_io_rx;
    io->base.tx_param = _board_i2c_panel_io_tx;
    io->base.tx_color = _board_i2c_panel_io_tx_color;
    io->base.del = _board_i2c_panel_io_delete;
    io->base.register_event_callbacks = _board_i2c_panel_io_register_callbacks;
    *out_io = &io->base;
    return result;

cleanup:
    if (io->lock != NULL)
    {
        vSemaphoreDelete(io->lock);
    }
    free(io);
    return result;
}

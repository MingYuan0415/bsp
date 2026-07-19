#include "board_imu.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define BOARD_IMU_WHO_AM_I_REG       (0x00U)
#define BOARD_IMU_WHO_AM_I_VALUE     (0x05U)
#define BOARD_IMU_CTRL1_REG          (0x02U)
#define BOARD_IMU_CTRL2_REG          (0x03U)
#define BOARD_IMU_CTRL3_REG          (0x04U)
#define BOARD_IMU_CTRL5_REG          (0x06U)
#define BOARD_IMU_CTRL7_REG          (0x08U)
#define BOARD_IMU_STATUS_INT_REG     (0x2DU)
#define BOARD_IMU_STATUS0_REG        (0x2EU)
#define BOARD_IMU_STATUS1_REG        (0x2FU)
#define BOARD_IMU_TIMESTAMP_REG      (0x30U)
#define BOARD_IMU_TEMPERATURE_REG    (0x33U)
#define BOARD_IMU_RESET_REG          (0x60U)
#define BOARD_IMU_RESET_VALUE        (0xB0U)

#define BOARD_IMU_CTRL7_ACCEL_ENABLE (1U << 0)
#define BOARD_IMU_CTRL7_GYRO_ENABLE  (1U << 1)
#define BOARD_IMU_CTRL1_AUTO_INC     (1U << 6)
#define BOARD_IMU_CTRL1_INT1_ENABLE  (1U << 3)
#define BOARD_IMU_CTRL7_DATA_INT_EN  (1U << 5)
#define BOARD_IMU_STATUS0_ACCEL_RDY  (1U << 0)
#define BOARD_IMU_STATUS0_GYRO_RDY   (1U << 1)

#define BOARD_IMU_DEFAULT_CLOCK_HZ    (400000U)
#define BOARD_IMU_DEFAULT_RATE_HZ     (100U)
#define BOARD_IMU_DEFAULT_ACCEL_RANGE BOARD_IMU_ACCEL_RANGE_4G
#define BOARD_IMU_DEFAULT_GYRO_RANGE  BOARD_IMU_GYRO_RANGE_64DPS
#define BOARD_IMU_RESET_DELAY_MS      (15U)

struct board_imu
{
    i2c_master_dev_handle_t i2c_device;
    esp_io_expander_handle_t io_expander;
    SemaphoreHandle_t lock;
    board_imu_config_t config;
    float accel_scale_g;
    float gyro_scale_dps;
    bool enabled;
    bool last_data_ready;
    bool last_sample_valid;
    bool interrupt_level;
    bool interrupt_level_valid;
    board_imu_interrupt_cb_t interrupt_callback;
    void *interrupt_user_data;
};

static uint8_t _board_imu_accel_odr(uint32_t requested_hz)
{
    /* QMI8658 has no exact 100 Hz setting; 125 Hz is the nearest supported ODR. */
    if (requested_hz >= 750U)
    {
        return 3U; /* 1000 Hz */
    }
    if (requested_hz >= 375U)
    {
        return 4U; /* 500 Hz */
    }
    if (requested_hz >= 188U)
    {
        return 5U; /* 250 Hz */
    }
    if (requested_hz >= 94U)
    {
        return 6U; /* 125 Hz */
    }
    if (requested_hz >= 47U)
    {
        return 7U; /* 62.5 Hz */
    }
    if (requested_hz >= 22U)
    {
        return 8U; /* 31.25 Hz */
    }
    if (requested_hz >= 16U)
    {
        return 13U; /* low-power 21 Hz */
    }
    if (requested_hz >= 7U)
    {
        return 14U; /* low-power 11 Hz */
    }
    return 15U; /* low-power 3 Hz */
}

static uint8_t _board_imu_gyro_odr(uint32_t requested_hz)
{
    if (requested_hz >= 3000U)
    {
        return 1U; /* 3587.2 Hz */
    }
    if (requested_hz >= 1500U)
    {
        return 2U; /* 1793.6 Hz */
    }
    if (requested_hz >= 750U)
    {
        return 3U; /* 896.8 Hz */
    }
    if (requested_hz >= 375U)
    {
        return 4U; /* 448.4 Hz */
    }
    if (requested_hz >= 188U)
    {
        return 5U; /* 224.2 Hz */
    }
    if (requested_hz >= 94U)
    {
        return 6U; /* 112.1 Hz */
    }
    if (requested_hz >= 47U)
    {
        return 7U; /* 56.05 Hz */
    }
    return 8U; /* 28.025 Hz */
}

static float _board_imu_accel_scale(board_imu_accel_range_t range)
{
    static const float ranges[] = {2.0F, 4.0F, 8.0F, 16.0F};
    const unsigned index = (unsigned)range;
    return (index < (sizeof(ranges) / sizeof(ranges[0]))) ?
           ranges[index] / 32768.0F : 4.0F / 32768.0F;
}

static float _board_imu_gyro_scale(board_imu_gyro_range_t range)
{
    static const float ranges[] =
    {
        16.0F, 32.0F, 64.0F, 128.0F, 256.0F, 512.0F, 1024.0F,
    };
    const unsigned index = (unsigned)range;
    return (index < (sizeof(ranges) / sizeof(ranges[0]))) ?
           ranges[index] / 32768.0F : 64.0F / 32768.0F;
}

static bool _board_imu_sample_rate_valid(uint32_t sample_rate_hz)
{
    return sample_rate_hz > 0U &&
           sample_rate_hz <= BOARD_IMU_SAMPLE_RATE_MAX_HZ;
}

void board_imu_config_default(board_imu_config_t *config)
{
    if (config == NULL)
    {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->i2c_address = BOARD_IMU_I2C_ADDRESS;
    config->i2c_clock_hz = BOARD_IMU_DEFAULT_CLOCK_HZ;
    config->sample_rate_hz = BOARD_IMU_DEFAULT_RATE_HZ;
    config->accel_range = BOARD_IMU_DEFAULT_ACCEL_RANGE;
    config->gyro_range = BOARD_IMU_DEFAULT_GYRO_RANGE;
    config->enable_interrupt = true;
}

static esp_err_t _board_imu_write_locked(board_imu_t *device,
        uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(device->i2c_device, data, sizeof(data),
                               BOARD_IMU_I2C_TIMEOUT_MS);
}

static esp_err_t _board_imu_read_locked(board_imu_t *device,
                                        uint8_t reg, uint8_t *data, size_t size)
{
    return i2c_master_transmit_receive(device->i2c_device, &reg, sizeof(reg),
                                       data, size, BOARD_IMU_I2C_TIMEOUT_MS);
}

static esp_err_t _board_imu_update_locked(board_imu_t *device,
        uint8_t reg, uint8_t clear_mask, uint8_t set_mask)
{
    uint8_t value = 0;
    esp_err_t result = _board_imu_read_locked(device, reg, &value, sizeof(value));
    if (result == ESP_OK)
    {
        value = (uint8_t)((value & (uint8_t)~clear_mask) | set_mask);
        result = _board_imu_write_locked(device, reg, value);
    }
    return result;
}

static esp_err_t _board_imu_reset_locked(board_imu_t *device)
{
    esp_err_t result = _board_imu_write_locked(device, BOARD_IMU_RESET_REG,
                       BOARD_IMU_RESET_VALUE);
    if (result != ESP_OK)
    {
        return result;
    }

    vTaskDelay(pdMS_TO_TICKS(BOARD_IMU_RESET_DELAY_MS));
    uint8_t who_am_i = 0;
    result = _board_imu_read_locked(device, BOARD_IMU_WHO_AM_I_REG,
                                    &who_am_i, sizeof(who_am_i));
    if (result == ESP_OK && who_am_i != BOARD_IMU_WHO_AM_I_VALUE)
    {
        result = ESP_ERR_NOT_FOUND;
    }
    return result;
}

static esp_err_t _board_imu_configure_locked(board_imu_t *device)
{
    const uint8_t accel_odr = _board_imu_accel_odr(device->config.sample_rate_hz);
    const uint8_t gyro_odr = _board_imu_gyro_odr(device->config.sample_rate_hz);
    esp_err_t result = _board_imu_write_locked(device, BOARD_IMU_CTRL1_REG,
                       (uint8_t)(BOARD_IMU_CTRL1_AUTO_INC |
                                 (device->config.enable_interrupt ?
                                  BOARD_IMU_CTRL1_INT1_ENABLE : 0U)));
    if (result != ESP_OK)
    {
        return result;
    }

    /* CTRL2/3: self-test disabled, range in bits 6:4, ODR in bits 3:0. */
    result = _board_imu_write_locked(device, BOARD_IMU_CTRL2_REG,
                                     (uint8_t)(
                                         ((uint8_t)device->config.accel_range << 4) |
                                         accel_odr));
    if (result != ESP_OK)
    {
        return result;
    }
    result = _board_imu_write_locked(device, BOARD_IMU_CTRL3_REG,
                                     (uint8_t)(
                                         ((uint8_t)device->config.gyro_range << 4) |
                                         gyro_odr));
    if (result != ESP_OK)
    {
        return result;
    }

    /* Enable both low-pass filters with the lowest filter bandwidth. */
    result = _board_imu_write_locked(device, BOARD_IMU_CTRL5_REG, 0x11U);
    if (result != ESP_OK)
    {
        return result;
    }

    const uint8_t data_ready_interrupt = device->config.enable_interrupt ? 0U :
                                         BOARD_IMU_CTRL7_DATA_INT_EN;
    result = _board_imu_write_locked(device, BOARD_IMU_CTRL7_REG,
                                     data_ready_interrupt);
    if (result == ESP_OK)
    {
        device->enabled = false;
    }
    return result;
}

esp_err_t board_imu_create(i2c_master_bus_handle_t i2c_bus,
                           esp_io_expander_handle_t io_expander,
                           const board_imu_config_t *config,
                           board_imu_t **out_device)
{
    if (i2c_bus == NULL || out_device == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *out_device = NULL;

    esp_err_t result = ESP_OK;
    board_imu_t *device = calloc(1, sizeof(*device));
    if (device == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    board_imu_config_default(&device->config);
    if (config != NULL)
    {
        device->config = *config;
        if (device->config.i2c_address == 0U)
        {
            device->config.i2c_address = BOARD_IMU_I2C_ADDRESS;
        }
        if (device->config.i2c_clock_hz == 0U)
        {
            device->config.i2c_clock_hz = BOARD_IMU_DEFAULT_CLOCK_HZ;
        }
        if (device->config.sample_rate_hz == 0U)
        {
            device->config.sample_rate_hz = BOARD_IMU_DEFAULT_RATE_HZ;
        }
    }
    if (device->config.i2c_address > 0x7FU ||
            !_board_imu_sample_rate_valid(device->config.sample_rate_hz) ||
            device->config.accel_range > BOARD_IMU_ACCEL_RANGE_16G ||
            device->config.gyro_range > BOARD_IMU_GYRO_RANGE_1024DPS)
    {
        result = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }
    device->io_expander = io_expander;
    device->accel_scale_g = _board_imu_accel_scale(device->config.accel_range);
    device->gyro_scale_dps = _board_imu_gyro_scale(device->config.gyro_range);

    device->lock = xSemaphoreCreateMutex();
    if (device->lock == NULL)
    {
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    const i2c_device_config_t i2c_config =
    {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device->config.i2c_address,
        .scl_speed_hz = device->config.i2c_clock_hz,
    };
    result = i2c_master_bus_add_device(i2c_bus, &i2c_config,
                                       &device->i2c_device);
    if (result != ESP_OK)
    {
        goto cleanup;
    }

    if (io_expander != NULL)
    {
        result = esp_io_expander_set_dir(io_expander, BOARD_IMU_INT1_PIN,
                                         IO_EXPANDER_INPUT);
        if (result != ESP_OK)
        {
            goto cleanup;
        }
    }

    xSemaphoreTake(device->lock, portMAX_DELAY);
    result = _board_imu_reset_locked(device);
    if (result == ESP_OK)
    {
        result = _board_imu_configure_locked(device);
    }
    xSemaphoreGive(device->lock);
    if (result != ESP_OK)
    {
        goto cleanup;
    }

    *out_device = device;
    return ESP_OK;

cleanup:
    if (device->i2c_device != NULL)
    {
        esp_err_t cleanup_result = i2c_master_bus_rm_device(device->i2c_device);
        if (cleanup_result != ESP_OK)
        {
            /* Preserve the live handle so board rollback can retry removal. */
            *out_device = device;
            return cleanup_result;
        }
        device->i2c_device = NULL;
    }
    if (device->lock != NULL)
    {
        vSemaphoreDelete(device->lock);
    }
    free(device);
    return result;
}

esp_err_t board_imu_destroy(board_imu_t *device)
{
    if (device == NULL)
    {
        return ESP_OK;
    }

    esp_err_t result = ESP_OK;
    if (device->i2c_device != NULL)
    {
        xSemaphoreTake(device->lock, portMAX_DELAY);
        if (device->enabled)
        {
            esp_err_t disable_result = _board_imu_update_locked(
                                           device, BOARD_IMU_CTRL7_REG,
                                           BOARD_IMU_CTRL7_ACCEL_ENABLE |
                                           BOARD_IMU_CTRL7_GYRO_ENABLE, 0U);
            if (disable_result != ESP_OK)
            {
                result = disable_result;
            }
            else
            {
                device->enabled = false;
            }
        }
        xSemaphoreGive(device->lock);
        if (result != ESP_OK)
        {
            return result;
        }

        result = i2c_master_bus_rm_device(device->i2c_device);
        if (result != ESP_OK)
        {
            return result;
        }
        device->i2c_device = NULL;
    }

    if (device->lock != NULL)
    {
        vSemaphoreDelete(device->lock);
        device->lock = NULL;
    }
    free(device);
    return ESP_OK;
}

bool board_imu_is_available(const board_imu_t *device)
{
    return device != NULL && device->i2c_device != NULL;
}

esp_err_t board_imu_set_sample_rate(board_imu_t *device,
                                    uint32_t sample_rate_hz)
{
    if (!board_imu_is_available(device) ||
            !_board_imu_sample_rate_valid(sample_rate_hz))
    {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(device->lock, portMAX_DELAY);
    if (device->enabled)
    {
        xSemaphoreGive(device->lock);
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t previous_accel = 0U;
    uint8_t previous_gyro = 0U;
    esp_err_t result = _board_imu_read_locked(device, BOARD_IMU_CTRL2_REG,
                       &previous_accel, sizeof(previous_accel));
    if (result == ESP_OK)
    {
        result = _board_imu_read_locked(device, BOARD_IMU_CTRL3_REG,
                                        &previous_gyro, sizeof(previous_gyro));
    }
    if (result != ESP_OK)
    {
        xSemaphoreGive(device->lock);
        return result;
    }

    const uint8_t next_accel = (uint8_t)(
                                   (previous_accel & 0xF0U) |
                                   _board_imu_accel_odr(sample_rate_hz));
    const uint8_t next_gyro = (uint8_t)(
                                  (previous_gyro & 0xF0U) |
                                  _board_imu_gyro_odr(sample_rate_hz));
    result = _board_imu_write_locked(device, BOARD_IMU_CTRL2_REG, next_accel);
    if (result == ESP_OK)
    {
        result = _board_imu_write_locked(device, BOARD_IMU_CTRL3_REG, next_gyro);
        if (result != ESP_OK)
        {
            const esp_err_t rollback_result = _board_imu_write_locked(
                                                  device, BOARD_IMU_CTRL2_REG,
                                                  previous_accel);
            if (rollback_result != ESP_OK)
            {
                result = rollback_result;
            }
        }
    }
    if (result == ESP_OK)
    {
        device->config.sample_rate_hz = sample_rate_hz;
    }
    xSemaphoreGive(device->lock);
    return result;
}

esp_err_t board_imu_set_enabled(board_imu_t *device, bool enabled)
{
    if (!board_imu_is_available(device))
    {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(device->lock, portMAX_DELAY);
    esp_err_t result = _board_imu_update_locked(
                           device, BOARD_IMU_CTRL7_REG,
                           BOARD_IMU_CTRL7_ACCEL_ENABLE |
                           BOARD_IMU_CTRL7_GYRO_ENABLE,
                           enabled ? (BOARD_IMU_CTRL7_ACCEL_ENABLE |
                                      BOARD_IMU_CTRL7_GYRO_ENABLE) : 0U);
    if (result == ESP_OK)
    {
        device->enabled = enabled;
    }
    xSemaphoreGive(device->lock);
    return result;
}

esp_err_t board_imu_read(board_imu_t *device, board_imu_sample_t *sample)
{
    if (!board_imu_is_available(device) || sample == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t status[3] = {0};
    uint8_t timestamp[3] = {0};
    uint8_t values[14] = {0};
    xSemaphoreTake(device->lock, portMAX_DELAY);
    esp_err_t result = _board_imu_read_locked(device, BOARD_IMU_STATUS_INT_REG,
                       status, sizeof(status));
    if (result == ESP_OK)
    {
        result = _board_imu_read_locked(device, BOARD_IMU_TIMESTAMP_REG,
                                        timestamp, sizeof(timestamp));
    }
    if (result == ESP_OK)
    {
        result = _board_imu_read_locked(device, BOARD_IMU_TEMPERATURE_REG,
                                        values, sizeof(values));
    }
    if (result != ESP_OK)
    {
        xSemaphoreGive(device->lock);
        return result;
    }

    memset(sample, 0, sizeof(*sample));
    sample->status_int = status[0];
    sample->status0 = status[1];
    sample->status1 = status[2];
    sample->data_ready = (status[1] & (BOARD_IMU_STATUS0_ACCEL_RDY |
                                       BOARD_IMU_STATUS0_GYRO_RDY)) != 0U;
    sample->sensor_timestamp = (uint32_t)timestamp[0] |
                               ((uint32_t)timestamp[1] << 8) |
                               ((uint32_t)timestamp[2] << 16);

    const int16_t temperature_raw = (int16_t)((uint16_t)values[1] << 8 |
                                    values[0]);
    sample->temperature_c = (float)temperature_raw / 256.0F;
    for (unsigned axis = 0; axis < 3U; ++axis)
    {
        const unsigned offset = axis * 2U;
        const int16_t accel_raw = (int16_t)((uint16_t)values[2U + offset + 1U] << 8 |
                                            values[2U + offset]);
        const int16_t gyro_raw = (int16_t)((uint16_t)values[8U + offset + 1U] << 8 |
                                           values[8U + offset]);
        sample->accel_g[axis] = (float)accel_raw * device->accel_scale_g;
        sample->gyro_dps[axis] = (float)gyro_raw * device->gyro_scale_dps;
    }
    device->last_data_ready = sample->data_ready;
    device->last_sample_valid = true;
    xSemaphoreGive(device->lock);
    return ESP_OK;
}

esp_err_t board_imu_get_last_data_ready(const board_imu_t *device,
                                        bool *ready)
{
    if (!board_imu_is_available(device) || ready == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(device->lock, portMAX_DELAY);
    esp_err_t result = ESP_OK;
    if (!device->last_sample_valid)
    {
        result = ESP_ERR_INVALID_STATE;
    }
    else
    {
        *ready = device->last_data_ready;
    }
    xSemaphoreGive(device->lock);
    return result;
}

esp_err_t board_imu_get_interrupt_level(board_imu_t *device, bool *active)
{
    if (!board_imu_is_available(device) || active == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (device->io_expander == NULL)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint32_t levels = 0;
    esp_err_t result = esp_io_expander_get_level(device->io_expander,
                       BOARD_IMU_INT1_PIN, &levels);
    if (result == ESP_OK)
    {
        *active = (levels & BOARD_IMU_INT1_PIN) != 0U;
    }
    return result;
}

esp_err_t board_imu_poll_interrupt(board_imu_t *device, bool *changed)
{
    if (!board_imu_is_available(device) || changed == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    bool active = false;
    esp_err_t result = board_imu_get_interrupt_level(device, &active);
    if (result != ESP_OK)
    {
        return result;
    }

    board_imu_interrupt_cb_t callback = NULL;
    void *user_data = NULL;
    xSemaphoreTake(device->lock, portMAX_DELAY);
    *changed = !device->interrupt_level_valid || device->interrupt_level != active;
    device->interrupt_level = active;
    device->interrupt_level_valid = true;
    if (*changed)
    {
        callback = device->interrupt_callback;
        user_data = device->interrupt_user_data;
    }
    xSemaphoreGive(device->lock);
    if (callback != NULL)
    {
        callback(active, user_data);
    }
    return ESP_OK;
}

esp_err_t board_imu_set_interrupt_callback(board_imu_t *device,
        board_imu_interrupt_cb_t callback,
        void *user_data)
{
    if (!board_imu_is_available(device))
    {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(device->lock, portMAX_DELAY);
    device->interrupt_callback = callback;
    device->interrupt_user_data = user_data;
    xSemaphoreGive(device->lock);
    return ESP_OK;
}

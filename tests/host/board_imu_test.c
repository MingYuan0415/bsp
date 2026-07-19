#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board_imu.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

struct fake_i2c_bus
{
    bool valid;
};

struct fake_i2c_device
{
    uint8_t registers[256];
    uint16_t address;
    uint32_t clock_hz;
    bool added;
    bool removed;
};

struct fake_io_expander
{
    uint32_t input_mask;
    uint32_t levels;
};

struct fake_semaphore
{
    bool created;
    bool locked;
};

static struct fake_i2c_device s_i2c_device;
static struct fake_semaphore s_semaphore;
static TickType_t s_delayed_ticks;
static unsigned s_interrupt_callbacks;
static unsigned s_write_failures_remaining;
static uint8_t s_write_failure_register;
static bool s_callback_level;

static void _assert_close(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.0001F);
}

static void _write_i16(uint8_t address, int16_t value)
{
    s_i2c_device.registers[address] = (uint8_t)value;
    s_i2c_device.registers[address + 1U] = (uint8_t)((uint16_t)value >> 8);
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                    const i2c_device_config_t *config,
                                    i2c_master_dev_handle_t *out_device)
{
    assert(bus != NULL);
    assert(config != NULL);
    assert(out_device != NULL);
    s_i2c_device.address = config->device_address;
    s_i2c_device.clock_hz = config->scl_speed_hz;
    s_i2c_device.added = true;
    *out_device = &s_i2c_device;
    return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    assert(device == &s_i2c_device);
    s_i2c_device.removed = true;
    return ESP_OK;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
                              const uint8_t *data, size_t size,
                              int timeout_ms)
{
    assert(device == &s_i2c_device);
    assert(data != NULL);
    assert(size == 2U);
    assert(timeout_ms == BOARD_IMU_I2C_TIMEOUT_MS);
    if (data[0] == s_write_failure_register &&
            s_write_failures_remaining > 0U)
    {
        --s_write_failures_remaining;
        return ESP_FAIL;
    }
    s_i2c_device.registers[data[0]] = data[1];
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
                                      const uint8_t *write_data,
                                      size_t write_size,
                                      uint8_t *read_data,
                                      size_t read_size,
                                      int timeout_ms)
{
    assert(device == &s_i2c_device);
    assert(write_data != NULL && write_size == 1U);
    assert(read_data != NULL);
    assert(timeout_ms == BOARD_IMU_I2C_TIMEOUT_MS);
    memcpy(read_data, &s_i2c_device.registers[write_data[0]], read_size);
    return ESP_OK;
}

esp_err_t esp_io_expander_set_dir(esp_io_expander_handle_t handle,
                                  uint32_t pin_mask,
                                  esp_io_expander_dir_t direction)
{
    assert(handle != NULL);
    assert(direction == IO_EXPANDER_INPUT);
    handle->input_mask |= pin_mask;
    return ESP_OK;
}

esp_err_t esp_io_expander_get_level(esp_io_expander_handle_t handle,
                                    uint32_t pin_mask,
                                    uint32_t *level_mask)
{
    assert(handle != NULL && level_mask != NULL);
    *level_mask = handle->levels & pin_mask;
    return ESP_OK;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    s_semaphore.created = true;
    return &s_semaphore;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks)
{
    assert(semaphore == &s_semaphore && semaphore->created);
    assert(!semaphore->locked);
    assert(ticks == portMAX_DELAY);
    semaphore->locked = true;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    assert(semaphore == &s_semaphore && semaphore->created);
    assert(semaphore->locked);
    semaphore->locked = false;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore)
{
    assert(semaphore == &s_semaphore && semaphore->created);
    assert(!semaphore->locked);
    semaphore->created = false;
}

void vTaskDelay(TickType_t ticks)
{
    s_delayed_ticks += ticks;
}

static void _interrupt_callback(bool active, void *user_data)
{
    assert(user_data == &s_interrupt_callbacks);
    ++s_interrupt_callbacks;
    s_callback_level = active;
}

static void _prepare_sample_registers(void)
{
    s_i2c_device.registers[0x2DU] = 0xAAU;
    s_i2c_device.registers[0x2EU] = 0x03U;
    s_i2c_device.registers[0x2FU] = 0x55U;
    s_i2c_device.registers[0x30U] = 0x56U;
    s_i2c_device.registers[0x31U] = 0x34U;
    s_i2c_device.registers[0x32U] = 0x12U;
    _write_i16(0x33U, (int16_t)(25.5F * 256.0F));
    _write_i16(0x35U, 16384);
    _write_i16(0x37U, -16384);
    _write_i16(0x39U, 8192);
    _write_i16(0x3BU, 16384);
    _write_i16(0x3DU, -32768);
    _write_i16(0x3FU, 8192);
}

int main(void)
{
    memset(&s_i2c_device, 0, sizeof(s_i2c_device));
    struct fake_i2c_bus bus = {.valid = true};
    struct fake_io_expander expander = {0};
    s_i2c_device.registers[0x00U] = 0x05U;

    board_imu_t *imu = NULL;
    assert(board_imu_create(&bus, &expander, NULL, &imu) == ESP_OK);
    assert(imu != NULL && board_imu_is_available(imu));
    assert(s_i2c_device.address == BOARD_IMU_I2C_ADDRESS);
    assert(s_i2c_device.clock_hz == 400000U);
    assert(s_delayed_ticks == 15U);
    assert((expander.input_mask & BOARD_IMU_INT1_PIN) != 0U);
    assert(s_i2c_device.registers[0x02U] == 0x48U);
    assert(s_i2c_device.registers[0x03U] == 0x16U);
    assert(s_i2c_device.registers[0x04U] == 0x26U);
    assert(s_i2c_device.registers[0x06U] == 0x11U);
    assert(s_i2c_device.registers[0x08U] == 0x00U);
    assert(board_imu_set_sample_rate(imu, 0U) == ESP_ERR_INVALID_ARG);
    assert(board_imu_set_sample_rate(
               imu, BOARD_IMU_SAMPLE_RATE_MAX_HZ + 1U) == ESP_ERR_INVALID_ARG);
    s_write_failure_register = 0x04U;
    s_write_failures_remaining = 1U;
    assert(board_imu_set_sample_rate(imu, 800U) == ESP_FAIL);
    assert(s_i2c_device.registers[0x03U] == 0x16U);
    assert(s_i2c_device.registers[0x04U] == 0x26U);
    assert(board_imu_set_sample_rate(imu, 400U) == ESP_OK);
    assert(s_i2c_device.registers[0x03U] == 0x14U);
    assert(s_i2c_device.registers[0x04U] == 0x24U);

    bool ready = false;
    assert(board_imu_get_last_data_ready(imu, &ready) ==
           ESP_ERR_INVALID_STATE);
    assert(board_imu_set_enabled(imu, true) == ESP_OK);
    assert((s_i2c_device.registers[0x08U] & 0x03U) == 0x03U);
    assert(board_imu_set_sample_rate(imu, 100U) == ESP_ERR_INVALID_STATE);

    _prepare_sample_registers();
    board_imu_sample_t sample;
    assert(board_imu_read(imu, &sample) == ESP_OK);
    _assert_close(sample.accel_g[0], 2.0F);
    _assert_close(sample.accel_g[1], -2.0F);
    _assert_close(sample.accel_g[2], 1.0F);
    _assert_close(sample.gyro_dps[0], 32.0F);
    _assert_close(sample.gyro_dps[1], -64.0F);
    _assert_close(sample.gyro_dps[2], 16.0F);
    _assert_close(sample.temperature_c, 25.5F);
    assert(sample.sensor_timestamp == 0x123456U);
    assert(sample.status_int == 0xAAU && sample.status0 == 0x03U &&
           sample.status1 == 0x55U && sample.data_ready);
    assert(board_imu_get_last_data_ready(imu, &ready) == ESP_OK && ready);

    assert(board_imu_set_interrupt_callback(imu, _interrupt_callback,
                                            &s_interrupt_callbacks) == ESP_OK);
    bool changed = false;
    assert(board_imu_poll_interrupt(imu, &changed) == ESP_OK && changed);
    assert(s_interrupt_callbacks == 1U && !s_callback_level);
    assert(board_imu_poll_interrupt(imu, &changed) == ESP_OK && !changed);
    expander.levels = BOARD_IMU_INT1_PIN;
    assert(board_imu_poll_interrupt(imu, &changed) == ESP_OK && changed);
    assert(s_interrupt_callbacks == 2U && s_callback_level);

    assert(board_imu_destroy(imu) == ESP_OK);
    assert((s_i2c_device.registers[0x08U] & 0x03U) == 0U);
    assert(s_i2c_device.removed);
    puts("board IMU regression passed");
    return 0;
}

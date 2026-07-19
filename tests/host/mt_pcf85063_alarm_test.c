#include "mt_pcf85063.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/semphr.h"

#define TEST_REG_CONTROL_2     0x01U
#define TEST_REG_ALARM_SECONDS 0x0BU
#define TEST_CTRL2_AIE         (1U << 7)
#define TEST_CTRL2_AF          (1U << 6)

struct fake_rtc_i2c_bus
{
    bool valid;
};

struct fake_rtc_i2c_device
{
    uint8_t registers[256];
    uint16_t address;
    uint32_t clock_hz;
    uint8_t fail_write_address;
    bool fail_next_write;
    bool removed;
};

struct fake_rtc_semaphore
{
    bool created;
};

static struct fake_rtc_i2c_device s_device;
static struct fake_rtc_semaphore s_semaphore;

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                    const i2c_device_config_t *config,
                                    i2c_master_dev_handle_t *out_device)
{
    assert(bus != NULL && config != NULL && out_device != NULL);
    s_device.address = config->device_address;
    s_device.clock_hz = config->scl_speed_hz;
    *out_device = &s_device;
    return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    assert(device == &s_device);
    s_device.removed = true;
    return ESP_OK;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
                              const uint8_t *data, size_t size,
                              int timeout_ms)
{
    assert(device == &s_device && data != NULL);
    assert(size >= 2U && size <= 8U && timeout_ms == 20);
    if (s_device.fail_next_write && data[0] == s_device.fail_write_address)
    {
        s_device.fail_next_write = false;
        return ESP_FAIL;
    }
    memcpy(&s_device.registers[data[0]], &data[1], size - 1U);
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
                                      const uint8_t *write_data,
                                      size_t write_size,
                                      uint8_t *read_data,
                                      size_t read_size,
                                      int timeout_ms)
{
    assert(device == &s_device && write_data != NULL && write_size == 1U);
    assert(read_data != NULL && timeout_ms == 20);
    memcpy(read_data, &s_device.registers[write_data[0]], read_size);
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
    assert(ticks == portMAX_DELAY);
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    assert(semaphore == &s_semaphore && semaphore->created);
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore)
{
    assert(semaphore == &s_semaphore && semaphore->created);
    semaphore->created = false;
}

int main(void)
{
    memset(&s_device, 0, sizeof(s_device));
    struct fake_rtc_i2c_bus bus = {.valid = true};
    mt_pcf85063_t *rtc = NULL;
    assert(mt_pcf85063_create(&bus, &rtc) == ESP_OK);
    assert(rtc != NULL && mt_pcf85063_is_ready(rtc));
    assert(s_device.address == 0x51U && s_device.clock_hz == 100000U);

    const mt_pcf85063_alarm_config_t invalid = {0};
    assert(mt_pcf85063_alarm_configure(rtc, &invalid) ==
           ESP_ERR_INVALID_ARG);

    s_device.registers[TEST_REG_CONTROL_2] = 0x70U;
    const mt_pcf85063_alarm_config_t config =
    {
        .match_second = true,
        .second = 5U,
        .match_hour = true,
        .hour = 23U,
        .match_day = true,
        .day = 31U,
    };
    assert(mt_pcf85063_alarm_configure(rtc, &config) == ESP_OK);
    assert(s_device.registers[TEST_REG_CONTROL_2] == 0xB0U);
    assert(s_device.registers[TEST_REG_ALARM_SECONDS] == 0x05U);
    assert(s_device.registers[TEST_REG_ALARM_SECONDS + 1U] == 0x80U);
    assert(s_device.registers[TEST_REG_ALARM_SECONDS + 2U] == 0x23U);
    assert(s_device.registers[TEST_REG_ALARM_SECONDS + 3U] == 0x31U);
    assert(s_device.registers[TEST_REG_ALARM_SECONDS + 4U] == 0x80U);

    mt_pcf85063_alarm_status_t status;
    assert(mt_pcf85063_alarm_get_status(rtc, &status) == ESP_OK);
    assert(status.enabled && !status.pending);
    s_device.registers[TEST_REG_CONTROL_2] |= TEST_CTRL2_AF;
    assert(mt_pcf85063_alarm_get_status(rtc, &status) == ESP_OK);
    assert(status.enabled && status.pending);
    assert(mt_pcf85063_alarm_clear(rtc) == ESP_OK);
    assert(s_device.registers[TEST_REG_CONTROL_2] == 0xB0U);
    assert(mt_pcf85063_alarm_disable(rtc) == ESP_OK);
    assert(s_device.registers[TEST_REG_CONTROL_2] == 0x30U);

    s_device.registers[TEST_REG_CONTROL_2] =
        TEST_CTRL2_AIE | TEST_CTRL2_AF | 0x30U;
    s_device.fail_write_address = TEST_REG_ALARM_SECONDS;
    s_device.fail_next_write = true;
    assert(mt_pcf85063_alarm_configure(rtc, &config) == ESP_FAIL);
    assert(s_device.registers[TEST_REG_CONTROL_2] == 0x30U);

    assert(mt_pcf85063_destroy(rtc) == ESP_OK);
    assert(s_device.removed);
    puts("PCF85063 alarm regression passed");
    return 0;
}

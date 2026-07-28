#include "board_tca9554.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/semphr.h"
#include "freertos/task.h"

#define TEST_DEVICE_ADDRESS        (0x20U)
#define TEST_I2C_SPEED_HZ          (400000U)
#define TEST_I2C_TIMEOUT_MS        (20)
#define TEST_INIT_DELAY_MS         (10U)
#define TEST_INIT_ATTEMPTS         (10U)
#define TEST_TRANSACTION_CAPACITY  (32U)

#define TEST_REG_OUTPUT            (0x01U)
#define TEST_REG_DIRECTION         (0x03U)
#define TEST_REGISTER_DEFAULT      (0xFFU)

struct fake_i2c_bus
{
    bool valid;
};

struct fake_i2c_device
{
    bool added;
    bool removed;
};

struct fake_semaphore
{
    bool created;
    bool locked;
    bool deleted;
};

typedef struct test_transaction
{
    uint8_t reg;
    uint8_t value;
} test_transaction_t;

typedef struct test_state
{
    struct fake_i2c_device device;
    struct fake_semaphore semaphore;
    esp_err_t add_result;
    esp_err_t remove_result;
    esp_err_t transfer_results[TEST_TRANSACTION_CAPACITY];
    size_t transfer_result_count;
    size_t transfer_result_index;
    test_transaction_t transactions[TEST_TRANSACTION_CAPACITY];
    size_t transaction_count;
    uint16_t device_address;
    uint32_t clock_hz;
    unsigned int delay_calls;
    TickType_t delayed_ticks;
} test_state_t;

static test_state_t s_test;

static void _test_reset(void)
{
    memset(&s_test, 0, sizeof(s_test));
    s_test.add_result = ESP_OK;
    s_test.remove_result = ESP_OK;
}

static void _test_queue_transfer_result(esp_err_t result)
{
    assert(s_test.transfer_result_count < TEST_TRANSACTION_CAPACITY);
    s_test.transfer_results[s_test.transfer_result_count++] = result;
}

static void _test_reset_transfer_observation(void)
{
    memset(s_test.transfer_results, 0, sizeof(s_test.transfer_results));
    s_test.transfer_result_count = 0U;
    s_test.transfer_result_index = 0U;
    memset(s_test.transactions, 0, sizeof(s_test.transactions));
    s_test.transaction_count = 0U;
    s_test.delay_calls = 0U;
    s_test.delayed_ticks = 0U;
}

static void _test_assert_transaction(size_t index, uint8_t reg, uint8_t value)
{
    assert(index < s_test.transaction_count);
    assert(s_test.transactions[index].reg == reg);
    assert(s_test.transactions[index].value == value);
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                    const i2c_device_config_t *config,
                                    i2c_master_dev_handle_t *out_device)
{
    assert(bus != NULL && config != NULL && out_device != NULL);
    if (s_test.add_result != ESP_OK)
    {
        return s_test.add_result;
    }
    assert(config->dev_addr_length == I2C_ADDR_BIT_LEN_7);
    s_test.device_address = config->device_address;
    s_test.clock_hz = config->scl_speed_hz;
    s_test.device.added = true;
    *out_device = &s_test.device;
    return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    assert(device == &s_test.device && s_test.device.added);
    if (s_test.remove_result != ESP_OK)
    {
        return s_test.remove_result;
    }
    s_test.device.removed = true;
    return ESP_OK;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
                              const uint8_t *data, size_t size,
                              int timeout_ms)
{
    assert(device == &s_test.device && data != NULL);
    assert(size == 2U && timeout_ms == TEST_I2C_TIMEOUT_MS);
    assert(s_test.transaction_count < TEST_TRANSACTION_CAPACITY);
    s_test.transactions[s_test.transaction_count++] = (test_transaction_t)
    {
        .reg = data[0],
        .value = data[1],
    };

    if (s_test.transfer_result_index < s_test.transfer_result_count)
    {
        return s_test.transfer_results[s_test.transfer_result_index++];
    }
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
                                      const uint8_t *write_data,
                                      size_t write_size,
                                      uint8_t *read_data,
                                      size_t read_size,
                                      int timeout_ms)
{
    assert(device == &s_test.device && write_data != NULL);
    assert(write_size == 1U && read_data != NULL && read_size == 1U);
    assert(timeout_ms == TEST_I2C_TIMEOUT_MS);
    *read_data = 0U;
    return ESP_OK;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    assert(!s_test.semaphore.created);
    s_test.semaphore.created = true;
    return &s_test.semaphore;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks)
{
    assert(semaphore == &s_test.semaphore && semaphore->created);
    assert(!semaphore->locked && ticks == portMAX_DELAY);
    semaphore->locked = true;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    assert(semaphore == &s_test.semaphore && semaphore->created);
    assert(semaphore->locked);
    semaphore->locked = false;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore)
{
    assert(semaphore == &s_test.semaphore && semaphore->created);
    assert(!semaphore->locked);
    semaphore->created = false;
    semaphore->deleted = true;
}

void vTaskDelay(TickType_t ticks)
{
    ++s_test.delay_calls;
    s_test.delayed_ticks += ticks;
}

static board_tca9554_t *_test_create(struct fake_i2c_bus *bus)
{
    board_tca9554_t *device = NULL;
    assert(board_tca9554_create(bus, TEST_DEVICE_ADDRESS, &device) == ESP_OK);
    assert(device != NULL);
    assert(s_test.device_address == TEST_DEVICE_ADDRESS);
    assert(s_test.clock_hz == TEST_I2C_SPEED_HZ);
    return device;
}

static void _test_successful_create_writes_safe_defaults(void)
{
    _test_reset();
    struct fake_i2c_bus bus = {.valid = true};
    board_tca9554_t *device = _test_create(&bus);

    assert(s_test.transaction_count == 2U);
    _test_assert_transaction(0U, TEST_REG_DIRECTION, TEST_REGISTER_DEFAULT);
    _test_assert_transaction(1U, TEST_REG_OUTPUT, TEST_REGISTER_DEFAULT);
    assert(s_test.delay_calls == 1U);
    assert(s_test.delayed_ticks == TEST_INIT_DELAY_MS);

    assert(board_tca9554_destroy(device) == ESP_OK);
    assert(s_test.device.removed && s_test.semaphore.deleted);
}

static void _test_direction_nack_retries_full_reset(void)
{
    _test_reset();
    _test_queue_transfer_result(ESP_ERR_INVALID_RESPONSE);
    _test_queue_transfer_result(ESP_OK);
    _test_queue_transfer_result(ESP_OK);
    struct fake_i2c_bus bus = {.valid = true};
    board_tca9554_t *device = _test_create(&bus);

    assert(s_test.transaction_count == 3U);
    _test_assert_transaction(0U, TEST_REG_DIRECTION, TEST_REGISTER_DEFAULT);
    _test_assert_transaction(1U, TEST_REG_DIRECTION, TEST_REGISTER_DEFAULT);
    _test_assert_transaction(2U, TEST_REG_OUTPUT, TEST_REGISTER_DEFAULT);
    assert(s_test.delay_calls == 2U);
    assert(s_test.delayed_ticks == 2U * TEST_INIT_DELAY_MS);

    assert(board_tca9554_destroy(device) == ESP_OK);
}

static void _test_output_nack_retries_full_reset(void)
{
    _test_reset();
    _test_queue_transfer_result(ESP_OK);
    _test_queue_transfer_result(ESP_ERR_INVALID_RESPONSE);
    _test_queue_transfer_result(ESP_OK);
    _test_queue_transfer_result(ESP_OK);
    struct fake_i2c_bus bus = {.valid = true};
    board_tca9554_t *device = _test_create(&bus);

    assert(s_test.transaction_count == 4U);
    _test_assert_transaction(0U, TEST_REG_DIRECTION, TEST_REGISTER_DEFAULT);
    _test_assert_transaction(1U, TEST_REG_OUTPUT, TEST_REGISTER_DEFAULT);
    _test_assert_transaction(2U, TEST_REG_DIRECTION, TEST_REGISTER_DEFAULT);
    _test_assert_transaction(3U, TEST_REG_OUTPUT, TEST_REGISTER_DEFAULT);
    assert(s_test.delay_calls == 2U);

    assert(board_tca9554_destroy(device) == ESP_OK);
}

static void _test_timeout_is_retried(void)
{
    _test_reset();
    _test_queue_transfer_result(ESP_ERR_TIMEOUT);
    _test_queue_transfer_result(ESP_OK);
    _test_queue_transfer_result(ESP_OK);
    struct fake_i2c_bus bus = {.valid = true};
    board_tca9554_t *device = _test_create(&bus);

    assert(s_test.transaction_count == 3U);
    assert(s_test.delay_calls == 2U);
    assert(board_tca9554_destroy(device) == ESP_OK);
}

static void _test_persistent_nack_exhausts_and_cleans_up(void)
{
    _test_reset();
    for (size_t index = 0U; index < TEST_INIT_ATTEMPTS; ++index)
    {
        _test_queue_transfer_result(ESP_ERR_INVALID_RESPONSE);
    }
    struct fake_i2c_bus bus = {.valid = true};
    board_tca9554_t *device = (board_tca9554_t *)(uintptr_t)1U;

    assert(board_tca9554_create(&bus, TEST_DEVICE_ADDRESS, &device) ==
           ESP_ERR_INVALID_RESPONSE);
    assert(device == NULL);
    assert(s_test.transaction_count == TEST_INIT_ATTEMPTS);
    assert(s_test.delay_calls == TEST_INIT_ATTEMPTS);
    assert(s_test.delayed_ticks == TEST_INIT_ATTEMPTS * TEST_INIT_DELAY_MS);
    assert(s_test.device.removed && s_test.semaphore.deleted);
}

static void _test_non_transient_error_is_not_retried(void)
{
    _test_reset();
    _test_queue_transfer_result(ESP_FAIL);
    struct fake_i2c_bus bus = {.valid = true};
    board_tca9554_t *device = NULL;

    assert(board_tca9554_create(&bus, TEST_DEVICE_ADDRESS, &device) == ESP_FAIL);
    assert(device == NULL);
    assert(s_test.transaction_count == 1U);
    assert(s_test.delay_calls == 1U);
    assert(s_test.delayed_ticks == TEST_INIT_DELAY_MS);
    assert(s_test.device.removed && s_test.semaphore.deleted);
}

static void _test_runtime_reset_remains_single_attempt(void)
{
    _test_reset();
    struct fake_i2c_bus bus = {.valid = true};
    board_tca9554_t *device = _test_create(&bus);
    esp_io_expander_handle_t expander = board_tca9554_get_expander(device);

    _test_reset_transfer_observation();
    _test_queue_transfer_result(ESP_ERR_INVALID_RESPONSE);
    assert(expander->reset(expander) == ESP_ERR_INVALID_RESPONSE);
    assert(s_test.transaction_count == 1U);
    assert(s_test.delay_calls == 0U);

    assert(board_tca9554_destroy(device) == ESP_OK);
}

static void _test_cleanup_failure_preserves_partial_handle(void)
{
    _test_reset();
    for (size_t index = 0U; index < TEST_INIT_ATTEMPTS; ++index)
    {
        _test_queue_transfer_result(ESP_ERR_INVALID_RESPONSE);
    }
    s_test.remove_result = ESP_ERR_INVALID_STATE;
    struct fake_i2c_bus bus = {.valid = true};
    board_tca9554_t *device = NULL;

    assert(board_tca9554_create(&bus, TEST_DEVICE_ADDRESS, &device) ==
           ESP_ERR_INVALID_STATE);
    assert(device != NULL);
    assert(!s_test.device.removed && !s_test.semaphore.deleted);

    s_test.remove_result = ESP_OK;
    assert(board_tca9554_destroy(device) == ESP_OK);
    assert(s_test.device.removed && s_test.semaphore.deleted);
}

int main(void)
{
    _test_successful_create_writes_safe_defaults();
    _test_direction_nack_retries_full_reset();
    _test_output_nack_retries_full_reset();
    _test_timeout_is_retried();
    _test_persistent_nack_exhausts_and_cleans_up();
    _test_non_transient_error_is_not_retried();
    _test_runtime_reset_remains_single_attempt();
    _test_cleanup_failure_preserves_partial_handle();
    printf("board_tca9554 tests passed\n");
    return 0;
}

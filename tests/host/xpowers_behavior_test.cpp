#include <array>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>

/* XPowersLib evaluates these macros in its host build as well. */
#define ESP_IDF_VERSION 0
#define ESP_IDF_VERSION_VAL(major, minor, patch) (0)
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

#include "freertos/semphr.h"
#include "board_power.h"
#include "mt_axp2101.h"

struct fake_i2c_bus
{
    int unused;
};

struct fake_i2c_device
{
    int unused;
};

struct fake_semaphore
{
    int unused;
};

struct board_tca9554
{
    int unused;
};

static std::array<uint8_t, 256> s_registers;
static fake_i2c_bus s_i2c_bus;
static fake_i2c_device s_i2c_device;
static esp_err_t s_remove_result = ESP_OK;

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                    const i2c_device_config_t *config,
                                    i2c_master_dev_handle_t *out_device)
{
    if (bus != &s_i2c_bus || config == nullptr || out_device == nullptr ||
            config->device_address != AXP2101_SLAVE_ADDRESS)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *out_device = &s_i2c_device;
    return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    return device == &s_i2c_device ? s_remove_result : ESP_ERR_INVALID_ARG;
}

esp_err_t board_tca9554_get_input_level(board_tca9554_t *device,
                                        uint8_t pin_mask,
                                        uint8_t *level_mask)
{
    (void)device;
    (void)pin_mask;
    if (level_mask == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *level_mask = 0U;
    return ESP_OK;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
                              const uint8_t *data, size_t size,
                              int timeout_ms)
{
    (void)timeout_ms;
    if (device != &s_i2c_device || data == nullptr || size < 2U ||
            static_cast<size_t>(data[0]) + size - 1U > s_registers.size())
    {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(&s_registers[data[0]], &data[1], size - 1U);
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
                                      const uint8_t *write_data,
                                      size_t write_size,
                                      uint8_t *read_data,
                                      size_t read_size,
                                      int timeout_ms)
{
    (void)timeout_ms;
    if (device != &s_i2c_device || write_data == nullptr ||
            write_size != 1U || read_data == nullptr ||
            static_cast<size_t>(write_data[0]) + read_size > s_registers.size())
    {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(read_data, &s_registers[write_data[0]], read_size);
    return ESP_OK;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    static fake_semaphore semaphore;
    return &semaphore;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks)
{
    (void)semaphore;
    (void)ticks;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    (void)semaphore;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore)
{
    (void)semaphore;
}

static int _read_registers(uint8_t device_address, uint8_t register_address,
                           uint8_t *data, uint8_t length)
{
    (void)device_address;
    int result = -1;
    if (data != nullptr &&
            static_cast<size_t>(register_address) + length <= s_registers.size())
    {
        memcpy(data, &s_registers[register_address], length);
        result = 0;
    }
    return result;
}

static int _write_registers(uint8_t device_address, uint8_t register_address,
                            uint8_t *data, uint8_t length)
{
    (void)device_address;
    int result = -1;
    if (data != nullptr &&
            static_cast<size_t>(register_address) + length <= s_registers.size())
    {
        memcpy(&s_registers[register_address], data, length);
        result = 0;
    }
    return result;
}

/**
 * @brief Verify the callback contract used by the board-level PMU wrapper.
 */
static void _test_axp2101_callback_contract(void)
{
    s_registers.fill(0);
    s_registers[XPOWERS_AXP2101_IC_TYPE] = XPOWERS_AXP2101_CHIP_ID;
    XPowersPMU power;

    assert(power.begin(AXP2101_SLAVE_ADDRESS,
                       _read_registers, _write_registers));
    assert(power.enableVbusVoltageMeasure());
    assert(power.enableBattVoltageMeasure());
    assert(power.enableSystemVoltageMeasure());
    assert(power.enableTemperatureMeasure());
    assert(power.disableTSPinMeasure());
    (void)power.getTemperature();
    (void)power.isCharging();
    (void)power.isDischarge();
    (void)power.isStandby();
    (void)power.isVbusIn();
    (void)power.isVbusGood();
    (void)power.getChargerStatus();
    (void)power.getBattVoltage();
    (void)power.getVbusVoltage();
    (void)power.getSystemVoltage();
    (void)power.isBatteryConnect();
    (void)power.getBatteryPercent();
    power.clearIrqStatus();
}

static void _test_board_default_profile(void)
{
    mt_axp2101_profile_t profile = {};
    mt_axp2101_profile_init_default(&profile);

    const mt_axp2101_rail_config_t expected[] =
    {
        {true, 3300},
        {true, 900},
        {true, 1200},
        {true, 1800},
        {false, 0},
        {true, 3300},
        {true, 3300},
        {true, 3000},
        {true, 1800},
        {true, 1200},
        {true, 2800},
        {true, 1200},
        {false, 0},
        {false, 0},
    };
    static_assert(sizeof(expected) / sizeof(expected[0]) ==
                  MT_AXP2101_RAIL_COUNT);
    for (size_t index = 0; index < MT_AXP2101_RAIL_COUNT; ++index)
    {
        assert(profile.rails[index].enable == expected[index].enable);
        assert(profile.rails[index].voltage_mv == expected[index].voltage_mv);
    }
    assert(profile.precharge_current_ma == 50U);
    assert(profile.charge_current_ma == 200U);
    assert(profile.termination_current_ma == 25U);
    assert(profile.charge_target_mv == 4100U);
    assert(profile.irq_enable_mask == MT_AXP2101_IRQ_DEFAULT);
}

static mt_axp2101_t *_create_board_pmu(void)
{
    s_registers.fill(0);
    s_registers[XPOWERS_AXP2101_IC_TYPE] = XPOWERS_AXP2101_CHIP_ID;
    mt_axp2101_t *device = nullptr;
    assert(mt_axp2101_create(&s_i2c_bus, &device) == ESP_OK);
    assert(device != nullptr);
    return device;
}

static void _test_board_irq_status_byte_order(void)
{
    mt_axp2101_t *device = _create_board_pmu();
    s_registers[XPOWERS_AXP2101_INTSTS1] = 0U;
    s_registers[XPOWERS_AXP2101_INTSTS2] = 0U;
    s_registers[XPOWERS_AXP2101_INTSTS3] = (1U << 3) | (1U << 4);

    uint32_t status = 0U;
    assert(mt_axp2101_get_irq_status(device, &status) == ESP_OK);
    assert(status == (MT_AXP2101_IRQ_CHARGE_START |
                      MT_AXP2101_IRQ_CHARGE_DONE));
    assert(mt_axp2101_destroy(device) == ESP_OK);
}

static void _test_board_precharge_range(void)
{
    mt_axp2101_t *device = _create_board_pmu();
    mt_axp2101_profile_t profile = {};
    mt_axp2101_profile_init_default(&profile);

    profile.precharge_current_ma = 75U;
    assert(mt_axp2101_apply_profile(device, &profile) == ESP_OK);
    assert((s_registers[XPOWERS_AXP2101_IPRECHG_SET] & 0x03U) == 0x03U);
    assert((s_registers[XPOWERS_AXP2101_CHARGE_GAUGE_WDT_CTRL] &
            ((1U << 3U) | (1U << 1U))) ==
           ((1U << 3U) | (1U << 1U)));

    s_registers[XPOWERS_AXP2101_IPRECHG_SET] = 0xA2U;
    profile.precharge_current_ma = 100U;
    assert(mt_axp2101_apply_profile(device, &profile) == ESP_ERR_INVALID_ARG);
    assert(s_registers[XPOWERS_AXP2101_IPRECHG_SET] == 0xA2U);
    assert(mt_axp2101_destroy(device) == ESP_OK);
}

static void _test_board_power_cleanup_ownership(void)
{
    s_registers.fill(0);
    s_registers[XPOWERS_AXP2101_IC_TYPE] = XPOWERS_AXP2101_CHIP_ID;
    assert(!board_power_has_resources());
    assert(board_power_init(&s_i2c_bus) == ESP_OK);
    assert(board_power_has_resources());

    s_remove_result = ESP_FAIL;
    assert(board_power_deinit() == ESP_FAIL);
    assert(board_power_has_resources());

    s_remove_result = ESP_OK;
    assert(board_power_deinit() == ESP_OK);
    assert(!board_power_has_resources());
}

int main(void)
{
    _test_axp2101_callback_contract();
    _test_board_default_profile();
    _test_board_irq_status_byte_order();
    _test_board_precharge_range();
    _test_board_power_cleanup_ownership();
    return 0;
}

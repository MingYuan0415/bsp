#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

static std::array<uint8_t, 256> s_registers;

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

int main(void)
{
    _test_axp2101_callback_contract();
    return 0;
}

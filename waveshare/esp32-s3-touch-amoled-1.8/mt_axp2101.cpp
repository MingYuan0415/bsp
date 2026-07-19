#include <array>
#include <cstring>
#include <new>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "driver/i2c_master.h"

#define DBG_TAG "axp2101"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#define XPOWERS_LOG_E(...) LOG_E(__VA_ARGS__)
#define XPOWERS_LOG_I(...) LOG_D(__VA_ARGS__)
#define XPOWERS_LOG_D(...) LOG_D(__VA_ARGS__)
#if !defined(ESP_PLATFORM)
    #define ESP_IDF_VERSION 0
    #define ESP_IDF_VERSION_VAL(major, minor, patch) (0)
#endif
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

#include "mt_axp2101.h"

#define MT_AXP2101_I2C_TIMEOUT_MS   (20)
#define MT_AXP2101_I2C_SPEED_HZ     (200000)
#define MT_AXP2101_WRITE_BUF_MAX    (65)

struct mt_axp2101
{
    XPowersPMU pmu;
    i2c_master_dev_handle_t pmu_dev = NULL;
    SemaphoreHandle_t lock = NULL;
    bool initialized = false;
    esp_err_t last_io_error = ESP_OK;
};

static mt_axp2101_t *s_callback_device = nullptr;

static int _mt_axp2101_register_read(uint8_t dev_addr, uint8_t reg_addr,
                                     uint8_t *data, uint8_t len)
{
    (void)dev_addr;
    int result = -1;

    if (len == 0)
    {
        result = 0;
    }
    else if (s_callback_device && s_callback_device->pmu_dev && data)
    {
        esp_err_t ret = i2c_master_transmit_receive(s_callback_device->pmu_dev,
                        &reg_addr,
                        sizeof(reg_addr),
                        data,
                        len,
                        MT_AXP2101_I2C_TIMEOUT_MS);
        if (ret != ESP_OK && s_callback_device->last_io_error == ESP_OK)
        {
            s_callback_device->last_io_error = ret;
        }
        result = ret == ESP_OK ? 0 : -1;
    }
    return result;
}

static int _mt_axp2101_register_write(uint8_t dev_addr, uint8_t reg_addr,
                                      uint8_t *data, uint8_t len)
{
    (void)dev_addr;
    int result = -1;

    if (len == 0)
    {
        result = 0;
    }
    else if (s_callback_device && s_callback_device->pmu_dev && data &&
             (len + 1U) <= MT_AXP2101_WRITE_BUF_MAX)
    {
        std::array<uint8_t, MT_AXP2101_WRITE_BUF_MAX> tx_buf = {0};
        tx_buf[0] = reg_addr;
        memcpy(&tx_buf[1], data, len);
        esp_err_t ret = i2c_master_transmit(s_callback_device->pmu_dev,
                                            tx_buf.data(),
                                            len + 1U,
                                            MT_AXP2101_I2C_TIMEOUT_MS);
        if (ret != ESP_OK && s_callback_device->last_io_error == ESP_OK)
        {
            s_callback_device->last_io_error = ret;
        }
        result = ret == ESP_OK ? 0 : -1;
    }
    return result;
}

static esp_err_t _mt_axp2101_io_result(mt_axp2101_t *device,
                                       bool operation_ok)
{
    if (device->last_io_error != ESP_OK)
    {
        return device->last_io_error;
    }
    return operation_ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static esp_err_t _mt_axp2101_write_register_byte(mt_axp2101_t *device,
        uint8_t address,
        uint8_t value)
{
    uint8_t data = value;
    if (_mt_axp2101_register_write(AXP2101_SLAVE_ADDRESS, address, &data, 1) != 0)
    {
        return device->last_io_error == ESP_OK ? ESP_FAIL : device->last_io_error;
    }
    return ESP_OK;
}

static esp_err_t _mt_axp2101_read_register_byte(mt_axp2101_t *device,
        uint8_t address,
        uint8_t *value)
{
    if (_mt_axp2101_register_read(AXP2101_SLAVE_ADDRESS, address, value, 1) != 0)
    {
        return device->last_io_error == ESP_OK ? ESP_FAIL : device->last_io_error;
    }
    return ESP_OK;
}

static esp_err_t _mt_axp2101_enable_fuel_gauge(mt_axp2101_t *device)
{
    uint8_t value = 0U;
    esp_err_t result = _mt_axp2101_read_register_byte(
                           device, XPOWERS_AXP2101_CHARGE_GAUGE_WDT_CTRL,
                           &value);
    if (result != ESP_OK)
    {
        return result;
    }
    value |= (1U << 3U);
    return _mt_axp2101_write_register_byte(
               device, XPOWERS_AXP2101_CHARGE_GAUGE_WDT_CTRL, value);
}

static mt_axp2101_charger_status_t _mt_axp2101_convert_charger_status(uint8_t raw_status)
{
    mt_axp2101_charger_status_t status = MT_AXP2101_CHARGER_UNKNOWN;
    switch (raw_status)
    {
    case XPOWERS_AXP2101_CHG_TRI_STATE:
        status = MT_AXP2101_CHARGER_TRI_STATE;
        break;
    case XPOWERS_AXP2101_CHG_PRE_STATE:
        status = MT_AXP2101_CHARGER_PRE_STATE;
        break;
    case XPOWERS_AXP2101_CHG_CC_STATE:
        status = MT_AXP2101_CHARGER_CC_STATE;
        break;
    case XPOWERS_AXP2101_CHG_CV_STATE:
        status = MT_AXP2101_CHARGER_CV_STATE;
        break;
    case XPOWERS_AXP2101_CHG_DONE_STATE:
        status = MT_AXP2101_CHARGER_DONE_STATE;
        break;
    case XPOWERS_AXP2101_CHG_STOP_STATE:
        status = MT_AXP2101_CHARGER_STOP_STATE;
        break;
    default:
        break;
    }
    return status;
}

static esp_err_t _mt_axp2101_cleanup(mt_axp2101_t *device)
{
    esp_err_t result = ESP_OK;
    if (!device)
    {
        goto exit;
    }

    if (device->pmu_dev)
    {
        result = i2c_master_bus_rm_device(device->pmu_dev);
        if (result != ESP_OK)
        {
            goto exit;
        }
        device->pmu_dev = NULL;
    }

    if (device->lock)
    {
        vSemaphoreDelete(device->lock);
        device->lock = NULL;
    }

    device->initialized = false;

    if (s_callback_device == device)
    {
        s_callback_device = nullptr;
    }

exit:
    return result;
}

static esp_err_t _mt_axp2101_fail_create(mt_axp2101_t *device,
        esp_err_t init_error,
        mt_axp2101_t **out_device)
{
    esp_err_t cleanup_error = _mt_axp2101_cleanup(device);
    esp_err_t result = init_error;
    if (cleanup_error != ESP_OK)
    {
        *out_device = device;
        result = cleanup_error;
        goto exit;
    }
    delete device;

exit:
    return result;
}

static esp_err_t _mt_axp2101_allocate(mt_axp2101_t **device)
{
    esp_err_t result = ESP_ERR_NO_MEM;
    *device = new (std::nothrow) mt_axp2101_t();
    if (*device != nullptr)
    {
        (*device)->lock = xSemaphoreCreateMutex();
        if ((*device)->lock != nullptr)
        {
            result = ESP_OK;
        }
    }
    return result;
}

static esp_err_t _mt_axp2101_add_i2c_device(
    i2c_master_bus_handle_t i2c_bus, mt_axp2101_t *device)
{
    i2c_device_config_t pmu_config = {};
    pmu_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    pmu_config.device_address = AXP2101_SLAVE_ADDRESS;
    pmu_config.scl_speed_hz = MT_AXP2101_I2C_SPEED_HZ;
    return i2c_master_bus_add_device(i2c_bus, &pmu_config,
                                     &device->pmu_dev);
}

static esp_err_t _mt_axp2101_initialize_pmu(mt_axp2101_t *device)
{
    esp_err_t result = ESP_OK;
    s_callback_device = device;
    device->last_io_error = ESP_OK;
    if (!device->pmu.begin(AXP2101_SLAVE_ADDRESS,
                           _mt_axp2101_register_read,
                           _mt_axp2101_register_write))
    {
        result = device->last_io_error == ESP_OK ?
                 ESP_FAIL : device->last_io_error;
    }

    if (result == ESP_OK &&
            (!device->pmu.enableVbusVoltageMeasure() ||
             !device->pmu.enableBattVoltageMeasure() ||
             !device->pmu.enableSystemVoltageMeasure() ||
             !device->pmu.enableTemperatureMeasure()))
    {
        result = device->last_io_error == ESP_OK ?
                 ESP_FAIL : device->last_io_error;
    }

    if (result == ESP_OK)
    {
        if (!device->pmu.disableTSPinMeasure())
        {
            LOG_W("disable TS pin measure failed");
        }
        device->pmu.clearIrqStatus();
        result = device->last_io_error;
    }
    return result;
}

esp_err_t mt_axp2101_create(i2c_master_bus_handle_t i2c_bus,
                            mt_axp2101_t **out_device)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    mt_axp2101_t *device = nullptr;
    if (i2c_bus == nullptr || out_device == nullptr)
    {
        goto exit;
    }
    if (s_callback_device != nullptr)
    {
        result = ESP_ERR_NOT_SUPPORTED;
        goto exit;
    }

    *out_device = nullptr;
    result = _mt_axp2101_allocate(&device);
    if (result == ESP_OK)
    {
        result = _mt_axp2101_add_i2c_device(i2c_bus, device);
    }
    if (result == ESP_OK)
    {
        result = _mt_axp2101_initialize_pmu(device);
    }
    if (result != ESP_OK && device != nullptr)
    {
        result = _mt_axp2101_fail_create(device, result, out_device);
    }
    else if (result == ESP_OK)
    {
        device->initialized = true;
        *out_device = device;
    }

exit:
    return result;
}

esp_err_t mt_axp2101_destroy(mt_axp2101_t *device)
{
    esp_err_t result = ESP_OK;
    if (!device)
    {
        goto exit;
    }

    result = _mt_axp2101_cleanup(device);
    if (result != ESP_OK)
    {
        goto exit;
    }
    delete device;

exit:
    return result;
}

bool mt_axp2101_is_ready(const mt_axp2101_t *device)
{
    return device && device->initialized;
}

esp_err_t mt_axp2101_get_power_info(
    mt_axp2101_t *device, mt_axp2101_power_info_t *power_info)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    bool lock_owned = false;
    mt_axp2101_power_info_t info = {};
    if (!device || !power_info)
    {
        goto exit;
    }

    if (!device->lock || s_callback_device != device)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    xSemaphoreTake(device->lock, portMAX_DELAY);
    lock_owned = true;

    if (!device->initialized)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    device->last_io_error = ESP_OK;

    info.chip_temperature_c = device->pmu.getTemperature();
    info.is_charging = device->pmu.isCharging();
    info.is_discharging = device->pmu.isDischarge();
    info.is_standby = device->pmu.isStandby();
    info.is_vbus_connected = device->pmu.isVbusIn();
    info.is_vbus_good = device->pmu.isVbusGood();
    info.charger_status = _mt_axp2101_convert_charger_status(
                              (uint8_t)device->pmu.getChargerStatus());

    info.battery_voltage_mv = device->pmu.getBattVoltage();
    info.vbus_voltage_mv = device->pmu.getVbusVoltage();
    info.system_voltage_mv = device->pmu.getSystemVoltage();

    if (device->pmu.isBatteryConnect())
    {
        int percent = device->pmu.getBatteryPercent();
        if (percent < 0)
        {
            info.battery_percent = -1;
        }
        else if (percent > 100)
        {
            info.battery_percent = 100;
        }
        else
        {
            info.battery_percent = (int8_t)percent;
        }
    }
    else
    {
        info.battery_percent = -1;
    }

    result = device->last_io_error;
    if (result == ESP_OK)
    {
        *power_info = info;
    }

exit:
    if (lock_owned)
    {
        xSemaphoreGive(device->lock);
    }
    return result;
}

const char *mt_axp2101_charger_status_to_string(mt_axp2101_charger_status_t status)
{
    const char *name = "unknown";
    switch (status)
    {
    case MT_AXP2101_CHARGER_TRI_STATE:
        name = "tri_charge";
        break;
    case MT_AXP2101_CHARGER_PRE_STATE:
        name = "pre_charge";
        break;
    case MT_AXP2101_CHARGER_CC_STATE:
        name = "constant_charge";
        break;
    case MT_AXP2101_CHARGER_CV_STATE:
        name = "constant_voltage";
        break;
    case MT_AXP2101_CHARGER_DONE_STATE:
        name = "charge_done";
        break;
    case MT_AXP2101_CHARGER_STOP_STATE:
        name = "not_charge";
        break;
    default:
        break;
    }
    return name;
}

void mt_axp2101_profile_init_default(mt_axp2101_profile_t *profile)
{
    if (profile == nullptr)
    {
        return;
    }

    *profile = {};
    profile->rails[MT_AXP2101_RAIL_DCDC1] = {true, 3300};
    profile->rails[MT_AXP2101_RAIL_DCDC2] = {true, 900};
    profile->rails[MT_AXP2101_RAIL_DCDC3] = {true, 1200};
    profile->rails[MT_AXP2101_RAIL_DCDC4] = {true, 1800};
    /* DCDC5 is marked NC on the schematic and must remain disabled. */
    profile->rails[MT_AXP2101_RAIL_DCDC5] = {false, 0};
    profile->rails[MT_AXP2101_RAIL_ALDO1] = {true, 3300};
    profile->rails[MT_AXP2101_RAIL_ALDO2] = {true, 3300};
    profile->rails[MT_AXP2101_RAIL_ALDO3] = {true, 3000};
    profile->rails[MT_AXP2101_RAIL_ALDO4] = {true, 1800};
    profile->rails[MT_AXP2101_RAIL_BLDO1] = {true, 1200};
    profile->rails[MT_AXP2101_RAIL_BLDO2] = {true, 2800};
    profile->rails[MT_AXP2101_RAIL_CPUSLDO] = {true, 1200};
    profile->rails[MT_AXP2101_RAIL_DLDO1] = {false, 0};
    profile->rails[MT_AXP2101_RAIL_DLDO2] = {false, 0};
    profile->precharge_current_ma = 50;
    profile->charge_current_ma = 200;
    profile->termination_current_ma = 25;
    profile->charge_target_mv = 4100;
    profile->irq_enable_mask = MT_AXP2101_IRQ_DEFAULT;
}

static esp_err_t _mt_axp2101_apply_rail(mt_axp2101_t *device,
                                        mt_axp2101_rail_t rail,
                                        const mt_axp2101_rail_config_t *config)
{
    if (rail < 0 || rail >= MT_AXP2101_RAIL_COUNT || config == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    bool operation_ok = true;
    if (config->voltage_mv != 0U)
    {
        switch (rail)
        {
        case MT_AXP2101_RAIL_DCDC1:
            operation_ok = device->pmu.setDC1Voltage(config->voltage_mv);
            break;
        case MT_AXP2101_RAIL_DCDC2:
            operation_ok = device->pmu.setDC2Voltage(config->voltage_mv);
            break;
        case MT_AXP2101_RAIL_DCDC3:
            operation_ok = device->pmu.setDC3Voltage(config->voltage_mv);
            break;
        case MT_AXP2101_RAIL_DCDC4:
            operation_ok = device->pmu.setDC4Voltage(config->voltage_mv);
            break;
        case MT_AXP2101_RAIL_DCDC5:
            operation_ok = device->pmu.setDC5Voltage(config->voltage_mv);
            break;
        case MT_AXP2101_RAIL_ALDO1:
            operation_ok = device->pmu.setALDO1Voltage(config->voltage_mv);
            break;
        case MT_AXP2101_RAIL_ALDO2:
            operation_ok = device->pmu.setALDO2Voltage(config->voltage_mv);
            break;
        case MT_AXP2101_RAIL_ALDO3:
            operation_ok = device->pmu.setALDO3Voltage(config->voltage_mv);
            break;
        case MT_AXP2101_RAIL_ALDO4:
            operation_ok = device->pmu.setALDO4Voltage(config->voltage_mv);
            break;
        case MT_AXP2101_RAIL_BLDO1:
            operation_ok = device->pmu.setBLDO1Voltage(config->voltage_mv);
            break;
        case MT_AXP2101_RAIL_BLDO2:
            operation_ok = device->pmu.setBLDO2Voltage(config->voltage_mv);
            break;
        case MT_AXP2101_RAIL_CPUSLDO:
            operation_ok = device->pmu.setCPUSLDOVoltage(config->voltage_mv);
            break;
        case MT_AXP2101_RAIL_DLDO1:
            operation_ok = device->pmu.setDLDO1Voltage(config->voltage_mv);
            break;
        case MT_AXP2101_RAIL_DLDO2:
            operation_ok = device->pmu.setDLDO2Voltage(config->voltage_mv);
            break;
        default:
            operation_ok = false;
            break;
        }
        esp_err_t result = _mt_axp2101_io_result(device, operation_ok);
        if (result != ESP_OK)
        {
            return result;
        }
    }

    switch (rail)
    {
    case MT_AXP2101_RAIL_DCDC1:
        operation_ok = config->enable ? device->pmu.enableDC1() : device->pmu.disableDC1();
        break;
    case MT_AXP2101_RAIL_DCDC2:
        operation_ok = config->enable ? device->pmu.enableDC2() : device->pmu.disableDC2();
        break;
    case MT_AXP2101_RAIL_DCDC3:
        operation_ok = config->enable ? device->pmu.enableDC3() : device->pmu.disableDC3();
        break;
    case MT_AXP2101_RAIL_DCDC4:
        operation_ok = config->enable ? device->pmu.enableDC4() : device->pmu.disableDC4();
        break;
    case MT_AXP2101_RAIL_DCDC5:
        operation_ok = config->enable ? device->pmu.enableDC5() : device->pmu.disableDC5();
        break;
    case MT_AXP2101_RAIL_ALDO1:
        operation_ok = config->enable ? device->pmu.enableALDO1() : device->pmu.disableALDO1();
        break;
    case MT_AXP2101_RAIL_ALDO2:
        operation_ok = config->enable ? device->pmu.enableALDO2() : device->pmu.disableALDO2();
        break;
    case MT_AXP2101_RAIL_ALDO3:
        operation_ok = config->enable ? device->pmu.enableALDO3() : device->pmu.disableALDO3();
        break;
    case MT_AXP2101_RAIL_ALDO4:
        operation_ok = config->enable ? device->pmu.enableALDO4() : device->pmu.disableALDO4();
        break;
    case MT_AXP2101_RAIL_BLDO1:
        operation_ok = config->enable ? device->pmu.enableBLDO1() : device->pmu.disableBLDO1();
        break;
    case MT_AXP2101_RAIL_BLDO2:
        operation_ok = config->enable ? device->pmu.enableBLDO2() : device->pmu.disableBLDO2();
        break;
    case MT_AXP2101_RAIL_CPUSLDO:
        operation_ok = config->enable ? device->pmu.enableCPUSLDO() : device->pmu.disableCPUSLDO();
        break;
    case MT_AXP2101_RAIL_DLDO1:
        operation_ok = config->enable ? device->pmu.enableDLDO1() : device->pmu.disableDLDO1();
        break;
    case MT_AXP2101_RAIL_DLDO2:
        operation_ok = config->enable ? device->pmu.enableDLDO2() : device->pmu.disableDLDO2();
        break;
    default:
        operation_ok = false;
        break;
    }
    return _mt_axp2101_io_result(device, operation_ok);
}

static esp_err_t _mt_axp2101_set_precharge_current(mt_axp2101_t *device,
        uint16_t current_ma)
{
    if (current_ma > 75U || (current_ma % 25U) != 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }
    device->pmu.setPrechargeCurr((xpowers_prechg_t)(current_ma / 25U));
    return _mt_axp2101_io_result(device, true);
}

static esp_err_t _mt_axp2101_set_charge_current(mt_axp2101_t *device,
        uint16_t current_ma)
{
    uint8_t option = 0xFFU;
    switch (current_ma)
    {
    case 0:
        option = XPOWERS_AXP2101_CHG_CUR_0MA;
        break;
    case 100:
        option = XPOWERS_AXP2101_CHG_CUR_100MA;
        break;
    case 125:
        option = XPOWERS_AXP2101_CHG_CUR_125MA;
        break;
    case 150:
        option = XPOWERS_AXP2101_CHG_CUR_150MA;
        break;
    case 175:
        option = XPOWERS_AXP2101_CHG_CUR_175MA;
        break;
    case 200:
        option = XPOWERS_AXP2101_CHG_CUR_200MA;
        break;
    case 300:
        option = XPOWERS_AXP2101_CHG_CUR_300MA;
        break;
    case 400:
        option = XPOWERS_AXP2101_CHG_CUR_400MA;
        break;
    case 500:
        option = XPOWERS_AXP2101_CHG_CUR_500MA;
        break;
    case 600:
        option = XPOWERS_AXP2101_CHG_CUR_600MA;
        break;
    case 700:
        option = XPOWERS_AXP2101_CHG_CUR_700MA;
        break;
    case 800:
        option = XPOWERS_AXP2101_CHG_CUR_800MA;
        break;
    case 900:
        option = XPOWERS_AXP2101_CHG_CUR_900MA;
        break;
    case 1000:
        option = XPOWERS_AXP2101_CHG_CUR_1000MA;
        break;
    default:
        break;
    }
    if (option == 0xFFU)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return _mt_axp2101_io_result(device,
                                 device->pmu.setChargerConstantCurr(option));
}

static esp_err_t _mt_axp2101_set_termination_current(mt_axp2101_t *device,
        uint16_t current_ma)
{
    if (current_ma > 200U || (current_ma % 25U) != 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value = 0U;
    esp_err_t result = _mt_axp2101_read_register_byte(
                           device, XPOWERS_AXP2101_ITERM_CHG_SET_CTRL, &value);
    if (result != ESP_OK)
    {
        return result;
    }
    value = (uint8_t)((value & 0xE0U) | 0x10U | (current_ma / 25U));
    return _mt_axp2101_write_register_byte(
               device, XPOWERS_AXP2101_ITERM_CHG_SET_CTRL, value);
}

static esp_err_t _mt_axp2101_set_target_voltage(mt_axp2101_t *device,
        uint16_t voltage_mv)
{
    uint8_t option = 0U;
    switch (voltage_mv)
    {
    case 4000:
        option = XPOWERS_AXP2101_CHG_VOL_4V;
        break;
    case 4100:
        option = XPOWERS_AXP2101_CHG_VOL_4V1;
        break;
    case 4200:
        option = XPOWERS_AXP2101_CHG_VOL_4V2;
        break;
    case 4350:
        option = XPOWERS_AXP2101_CHG_VOL_4V35;
        break;
    case 4400:
        option = XPOWERS_AXP2101_CHG_VOL_4V4;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }
    return _mt_axp2101_io_result(device,
                                 device->pmu.setChargeTargetVoltage(option));
}

static esp_err_t _mt_axp2101_set_irq_mask_unlocked(mt_axp2101_t *device,
        uint32_t mask)
{
    device->last_io_error = ESP_OK;
    bool operation_ok = device->pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    if (operation_ok)
    {
        device->pmu.clearIrqStatus();
        operation_ok = device->pmu.enableIRQ(mask);
    }
    return _mt_axp2101_io_result(device, operation_ok);
}

esp_err_t mt_axp2101_apply_profile(mt_axp2101_t *device,
                                   const mt_axp2101_profile_t *profile)
{
    if (device == nullptr || profile == nullptr || device->lock == nullptr ||
            !device->initialized || s_callback_device != device)
    {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(device->lock, portMAX_DELAY);
    esp_err_t result = ESP_OK;
    device->last_io_error = ESP_OK;
    for (int rail = 0; rail < MT_AXP2101_RAIL_COUNT; ++rail)
    {
        result = _mt_axp2101_apply_rail(device,
                                        (mt_axp2101_rail_t)rail,
                                        &profile->rails[rail]);
        if (result != ESP_OK)
        {
            goto exit;
        }
    }
    result = _mt_axp2101_set_precharge_current(device,
             profile->precharge_current_ma);
    if (result != ESP_OK)
    {
        goto exit;
    }
    result = _mt_axp2101_set_charge_current(device, profile->charge_current_ma);
    if (result != ESP_OK)
    {
        goto exit;
    }
    result = _mt_axp2101_set_termination_current(
                 device, profile->termination_current_ma);
    if (result != ESP_OK)
    {
        goto exit;
    }
    result = _mt_axp2101_set_target_voltage(device, profile->charge_target_mv);
    if (result != ESP_OK)
    {
        goto exit;
    }
    result = _mt_axp2101_io_result(device,
                                   device->pmu.enableBattDetection());
    if (result != ESP_OK)
    {
        goto exit;
    }
    result = _mt_axp2101_enable_fuel_gauge(device);
    if (result != ESP_OK)
    {
        goto exit;
    }
    device->pmu.enableCellbatteryCharge();
    result = _mt_axp2101_io_result(device, true);
    if (result != ESP_OK)
    {
        goto exit;
    }
    result = _mt_axp2101_set_irq_mask_unlocked(device,
             profile->irq_enable_mask);

exit:
    xSemaphoreGive(device->lock);
    return result;
}

static void _mt_axp2101_read_rail_unlocked(mt_axp2101_t *device,
        mt_axp2101_rail_t rail,
        mt_axp2101_rail_info_t *info)
{
    switch (rail)
    {
    case MT_AXP2101_RAIL_DCDC1:
        info->enabled = device->pmu.isEnableDC1();
        info->voltage_mv = device->pmu.getDC1Voltage();
        break;
    case MT_AXP2101_RAIL_DCDC2:
        info->enabled = device->pmu.isEnableDC2();
        info->voltage_mv = device->pmu.getDC2Voltage();
        break;
    case MT_AXP2101_RAIL_DCDC3:
        info->enabled = device->pmu.isEnableDC3();
        info->voltage_mv = device->pmu.getDC3Voltage();
        break;
    case MT_AXP2101_RAIL_DCDC4:
        info->enabled = device->pmu.isEnableDC4();
        info->voltage_mv = device->pmu.getDC4Voltage();
        break;
    case MT_AXP2101_RAIL_DCDC5:
        info->enabled = device->pmu.isEnableDC5();
        info->voltage_mv = device->pmu.getDC5Voltage();
        break;
    case MT_AXP2101_RAIL_ALDO1:
        info->enabled = device->pmu.isEnableALDO1();
        info->voltage_mv = device->pmu.getALDO1Voltage();
        break;
    case MT_AXP2101_RAIL_ALDO2:
        info->enabled = device->pmu.isEnableALDO2();
        info->voltage_mv = device->pmu.getALDO2Voltage();
        break;
    case MT_AXP2101_RAIL_ALDO3:
        info->enabled = device->pmu.isEnableALDO3();
        info->voltage_mv = device->pmu.getALDO3Voltage();
        break;
    case MT_AXP2101_RAIL_ALDO4:
        info->enabled = device->pmu.isEnableALDO4();
        info->voltage_mv = device->pmu.getALDO4Voltage();
        break;
    case MT_AXP2101_RAIL_BLDO1:
        info->enabled = device->pmu.isEnableBLDO1();
        info->voltage_mv = device->pmu.getBLDO1Voltage();
        break;
    case MT_AXP2101_RAIL_BLDO2:
        info->enabled = device->pmu.isEnableBLDO2();
        info->voltage_mv = device->pmu.getBLDO2Voltage();
        break;
    case MT_AXP2101_RAIL_CPUSLDO:
        info->enabled = device->pmu.isEnableCPUSLDO();
        info->voltage_mv = device->pmu.getCPUSLDOVoltage();
        break;
    case MT_AXP2101_RAIL_DLDO1:
        info->enabled = device->pmu.isEnableDLDO1();
        info->voltage_mv = device->pmu.getDLDO1Voltage();
        break;
    case MT_AXP2101_RAIL_DLDO2:
        info->enabled = device->pmu.isEnableDLDO2();
        info->voltage_mv = device->pmu.getDLDO2Voltage();
        break;
    default:
        info->enabled = false;
        info->voltage_mv = 0U;
        break;
    }
}

esp_err_t mt_axp2101_get_rail_info(mt_axp2101_t *device,
                                   mt_axp2101_rail_t rail,
                                   mt_axp2101_rail_info_t *info)
{
    if (device == nullptr || info == nullptr || rail < 0 ||
            rail >= MT_AXP2101_RAIL_COUNT || device->lock == nullptr ||
            !device->initialized || s_callback_device != device)
    {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(device->lock, portMAX_DELAY);
    device->last_io_error = ESP_OK;
    mt_axp2101_rail_info_t readback = {};
    _mt_axp2101_read_rail_unlocked(device, rail, &readback);
    esp_err_t result = device->last_io_error;
    if (result == ESP_OK)
    {
        *info = readback;
    }
    xSemaphoreGive(device->lock);
    return result;
}

esp_err_t mt_axp2101_get_irq_status(mt_axp2101_t *device, uint32_t *status)
{
    if (device == nullptr || status == nullptr || device->lock == nullptr ||
            !device->initialized || s_callback_device != device)
    {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(device->lock, portMAX_DELAY);
    device->last_io_error = ESP_OK;
    const uint32_t irq_status =
        static_cast<uint32_t>(device->pmu.getIrqStatus());
    esp_err_t result = device->last_io_error;
    if (result == ESP_OK)
    {
        *status = irq_status;
    }
    xSemaphoreGive(device->lock);
    return result;
}

esp_err_t mt_axp2101_clear_irq_status(mt_axp2101_t *device)
{
    if (device == nullptr || device->lock == nullptr || !device->initialized ||
            s_callback_device != device)
    {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(device->lock, portMAX_DELAY);
    device->last_io_error = ESP_OK;
    device->pmu.clearIrqStatus();
    esp_err_t result = device->last_io_error;
    xSemaphoreGive(device->lock);
    return result;
}

esp_err_t mt_axp2101_set_irq_mask(mt_axp2101_t *device, uint32_t mask)
{
    if (device == nullptr || device->lock == nullptr || !device->initialized ||
            s_callback_device != device)
    {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(device->lock, portMAX_DELAY);
    esp_err_t result = _mt_axp2101_set_irq_mask_unlocked(device, mask);
    xSemaphoreGive(device->lock);
    return result;
}

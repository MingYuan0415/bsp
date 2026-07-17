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

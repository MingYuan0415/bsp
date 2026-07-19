/**
 * @brief  Board power management via AXP2101 PMU
 * @note   Adapted from reference project; maps mt_axp2101_power_info_t to
 *         bsp_power_info_t and registers bsp_power_ops_t.
 */

#include "board_power.h"

#include "bsp_hal.h"
#include "esp_err.h"
#include "mt_axp2101.h"

static mt_axp2101_t         *s_pmu      = NULL;
static bool                  s_power_registered = false;

static bool _board_power_is_available(void)
{
    return (s_pmu != NULL) && mt_axp2101_is_ready(s_pmu);
}

static esp_err_t _board_power_get_info_impl(bsp_power_info_t *info)
{
    if (!info)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!_board_power_is_available())
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    mt_axp2101_power_info_t axp_info = {0};
    esp_err_t result = mt_axp2101_get_power_info(s_pmu, &axp_info);
    if (result != ESP_OK)
    {
        return result;
    }

    info->battery_voltage_mv = axp_info.battery_voltage_mv;
    info->battery_percent    = axp_info.battery_percent;
    info->is_charging        = axp_info.is_charging;
    info->is_vbus_connected  = axp_info.is_vbus_connected;
    info->is_discharging     = axp_info.is_discharging;
    info->is_standby         = axp_info.is_standby;
    info->is_vbus_good       = axp_info.is_vbus_good;
    info->vbus_voltage_mv    = axp_info.vbus_voltage_mv;
    info->system_voltage_mv  = axp_info.system_voltage_mv;

    return result;
}

static bsp_power_ops_t s_power_ops =
{
    .is_available = _board_power_is_available,
    .get_info     = _board_power_get_info_impl,
};

esp_err_t board_power_init(i2c_master_bus_handle_t i2c_bus)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (!i2c_bus)
    {
        return result;
    }

    if (s_pmu != NULL && !mt_axp2101_is_ready(s_pmu))
    {
        esp_err_t cleanup_ret = mt_axp2101_destroy(s_pmu);
        if (cleanup_ret != ESP_OK)
        {
            return cleanup_ret;
        }
        s_pmu = NULL;
        s_power_registered = false;
    }
    if (s_pmu != NULL && s_power_registered)
    {
        return ESP_OK;
    }

    result = ESP_OK;
    if (s_pmu == NULL)
    {
        result = mt_axp2101_create(i2c_bus, &s_pmu);
        if (result != ESP_OK)
        {
            return result;
        }
    }

    result = bsp_hal_register_power(&s_power_ops);
    if (result != ESP_OK)
    {
        esp_err_t destroy_ret = mt_axp2101_destroy(s_pmu);
        if (destroy_ret == ESP_OK)
        {
            s_pmu = NULL;
        }
        s_power_registered = false;
        return result;
    }
    s_power_registered = true;

    return result;
}

esp_err_t board_power_deinit(void)
{
    if (s_pmu == NULL)
    {
        return ESP_OK;
    }

    esp_err_t result = mt_axp2101_destroy(s_pmu);
    if (result == ESP_OK)
    {
        s_pmu = NULL;
        s_power_registered = false;
    }
    return result;
}

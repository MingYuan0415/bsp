/**
 * @brief  Board power management via AXP2101 PMU
 * @note   Adapted from reference project; maps mt_axp2101_power_info_t to
 *         bsp_power_info_t for the board-level operation table.
 */

#include "board_power.h"

#include "esp_err.h"
#include "mt_axp2101.h"

static mt_axp2101_t         *s_pmu      = NULL;
static bool                  s_profile_applied = false;

bool board_power_is_available(void)
{
    return (s_pmu != NULL) && mt_axp2101_is_ready(s_pmu);
}

bool board_power_has_resources(void)
{
    return s_pmu != NULL;
}

esp_err_t board_power_get_info(bsp_power_info_t *info)
{
    if (!info)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!board_power_is_available())
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
        s_profile_applied = false;
    }
    if (s_pmu != NULL && s_profile_applied)
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

    if (!s_profile_applied)
    {
        mt_axp2101_profile_t profile;
        board_power_profile_init_default(&profile);
        result = mt_axp2101_apply_profile(s_pmu, &profile);
        if (result != ESP_OK)
        {
            esp_err_t destroy_ret = mt_axp2101_destroy(s_pmu);
            if (destroy_ret == ESP_OK)
            {
                s_pmu = NULL;
            }
            s_profile_applied = false;
            return result;
        }
        s_profile_applied = true;
    }

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
        s_profile_applied = false;
    }
    return result;
}

void board_power_profile_init_default(mt_axp2101_profile_t *profile)
{
    mt_axp2101_profile_init_default(profile);
}

esp_err_t board_power_apply_profile(const mt_axp2101_profile_t *profile)
{
    if (s_pmu == NULL || !mt_axp2101_is_ready(s_pmu))
    {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = mt_axp2101_apply_profile(s_pmu, profile);
    if (result == ESP_OK)
    {
        s_profile_applied = true;
    }
    return result;
}

esp_err_t board_power_get_rail_info(mt_axp2101_rail_t rail,
                                    mt_axp2101_rail_info_t *info)
{
    if (s_pmu == NULL || !mt_axp2101_is_ready(s_pmu))
    {
        return ESP_ERR_INVALID_STATE;
    }
    return mt_axp2101_get_rail_info(s_pmu, rail, info);
}

esp_err_t board_power_get_irq_status(uint32_t *status)
{
    if (s_pmu == NULL || !mt_axp2101_is_ready(s_pmu))
    {
        return ESP_ERR_INVALID_STATE;
    }
    return mt_axp2101_get_irq_status(s_pmu, status);
}

esp_err_t board_power_clear_irq_status(void)
{
    if (s_pmu == NULL || !mt_axp2101_is_ready(s_pmu))
    {
        return ESP_ERR_INVALID_STATE;
    }
    return mt_axp2101_clear_irq_status(s_pmu);
}

esp_err_t board_power_set_irq_mask(uint32_t mask)
{
    if (s_pmu == NULL || !mt_axp2101_is_ready(s_pmu))
    {
        return ESP_ERR_INVALID_STATE;
    }
    return mt_axp2101_set_irq_mask(s_pmu, mask);
}

mt_axp2101_t *board_power_get_pmu(void)
{
    return s_pmu;
}

esp_err_t board_power_poll_irq(board_tca9554_t *io_expander,
                               uint8_t irq_pin_mask,
                               board_power_irq_snapshot_t *snapshot)
{
    if (io_expander == NULL || snapshot == NULL || irq_pin_mask == 0U ||
            (irq_pin_mask & (uint8_t)(irq_pin_mask - 1U)) != 0U)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *snapshot = (board_power_irq_snapshot_t)
    {
        0
    };

    uint8_t level = 0U;
    esp_err_t result = board_tca9554_get_input_level(
                           io_expander, irq_pin_mask, &level);
    if (result != ESP_OK)
    {
        return result;
    }
    snapshot->line_active = (level & irq_pin_mask) == 0U;
    if (!snapshot->line_active)
    {
        return ESP_OK;
    }

    result = board_power_get_irq_status(&snapshot->status);
    if (result == ESP_OK)
    {
        result = board_power_clear_irq_status();
    }
    return result;
}

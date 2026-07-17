#ifndef __MT_AXP2101_H__
#define __MT_AXP2101_H__

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque AXP2101 PMU instance. */
typedef struct mt_axp2101 mt_axp2101_t;

/** @brief AXP2101 charger state reported by the PMU. */
typedef enum
{
    MT_AXP2101_CHARGER_TRI_STATE = 0,
    MT_AXP2101_CHARGER_PRE_STATE = 1,
    MT_AXP2101_CHARGER_CC_STATE = 2,
    MT_AXP2101_CHARGER_CV_STATE = 3,
    MT_AXP2101_CHARGER_DONE_STATE = 4,
    MT_AXP2101_CHARGER_STOP_STATE = 5,
    MT_AXP2101_CHARGER_UNKNOWN = 255,
} mt_axp2101_charger_status_t;

/** @brief Snapshot of AXP2101 power and charger telemetry. */
typedef struct mt_axp2101_power_info
{
    float chip_temperature_c;                  /**< PMU temperature. */
    bool is_charging;                          /**< Charging state. */
    bool is_discharging;                       /**< Discharging state. */
    bool is_standby;                           /**< Standby state. */
    bool is_vbus_connected;                    /**< VBUS presence. */
    bool is_vbus_good;                         /**< Valid VBUS state. */
    mt_axp2101_charger_status_t charger_status; /**< Charger phase. */
    uint16_t battery_voltage_mv;               /**< Battery millivolts. */
    uint16_t vbus_voltage_mv;                  /**< VBUS millivolts. */
    uint16_t system_voltage_mv;                /**< System millivolts. */
    int8_t battery_percent;                    /**< Percent, or -1 if absent. */
} mt_axp2101_power_info_t;

/**
 * @brief Create and initialize an AXP2101 PMU instance.
 *
 * @note When rollback fails, an error is returned and out_device receives the
 *       partial instance so mt_axp2101_destroy() can be retried.
 *
 * @param i2c_bus is the initialized I2C master bus.
 * @param out_device receives the owned PMU instance.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t mt_axp2101_create(i2c_master_bus_handle_t i2c_bus,
                            mt_axp2101_t **out_device);

/**
 * @brief Remove the I2C device and release an AXP2101 instance.
 *
 * @param device is the instance to release; NULL is accepted.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t mt_axp2101_destroy(mt_axp2101_t *device);

/**
 * @brief Report whether an AXP2101 instance completed initialization.
 *
 * @param device is the PMU instance to inspect.
 *
 * @return true when ready; false otherwise.
 */
bool mt_axp2101_is_ready(const mt_axp2101_t *device);

/**
 * @brief Read a consistent PMU telemetry snapshot.
 *
 * @param device is the initialized PMU instance.
 * @param power_info receives telemetry after all I2C reads succeed.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t mt_axp2101_get_power_info(
    mt_axp2101_t *device, mt_axp2101_power_info_t *power_info);

/**
 * @brief Return a stable name for a charger state.
 *
 * @param status is the charger state to describe.
 *
 * @return Static state name; unknown values return "unknown".
 */
const char *mt_axp2101_charger_status_to_string(
    mt_axp2101_charger_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* __MT_AXP2101_H__ */

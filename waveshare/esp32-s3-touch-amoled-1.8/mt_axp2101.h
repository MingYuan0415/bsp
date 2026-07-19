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

/** @brief AXP2101 power rail identifiers. */
typedef enum
{
    MT_AXP2101_RAIL_DCDC1 = 0,
    MT_AXP2101_RAIL_DCDC2,
    MT_AXP2101_RAIL_DCDC3,
    MT_AXP2101_RAIL_DCDC4,
    MT_AXP2101_RAIL_DCDC5,
    MT_AXP2101_RAIL_ALDO1,
    MT_AXP2101_RAIL_ALDO2,
    MT_AXP2101_RAIL_ALDO3,
    MT_AXP2101_RAIL_ALDO4,
    MT_AXP2101_RAIL_BLDO1,
    MT_AXP2101_RAIL_BLDO2,
    MT_AXP2101_RAIL_CPUSLDO,
    MT_AXP2101_RAIL_DLDO1,
    MT_AXP2101_RAIL_DLDO2,
    MT_AXP2101_RAIL_COUNT,
} mt_axp2101_rail_t;

/** @brief Requested or read back state of one AXP2101 rail. */
typedef struct mt_axp2101_rail_config
{
    bool enable;                 /**< Whether the rail should be enabled. */
    uint16_t voltage_mv;         /**< Requested or measured setting. */
} mt_axp2101_rail_config_t;

/**
 * @brief Complete AXP2101 board power profile.
 *
 * A voltage of zero means that the corresponding rail is left unchanged when
 * applying a profile.  This allows callers to update only charger settings.
 */
typedef struct mt_axp2101_profile
{
    mt_axp2101_rail_config_t rails[MT_AXP2101_RAIL_COUNT];
    uint16_t precharge_current_ma; /**< Supported values: 0, 25, 50, 75 mA. */
    uint16_t charge_current_ma;
    uint16_t termination_current_ma;
    uint16_t charge_target_mv;
    uint32_t irq_enable_mask;
} mt_axp2101_profile_t;

/** @brief Read-back state for one AXP2101 rail. */
typedef struct mt_axp2101_rail_info
{
    bool enabled;
    uint16_t voltage_mv;
} mt_axp2101_rail_info_t;

/* AXP2101 IRQ bits exposed without requiring the C++ XPowers headers. */
#define MT_AXP2101_IRQ_BAT_INSERT       (1UL << 13)
#define MT_AXP2101_IRQ_BAT_REMOVE       (1UL << 12)
#define MT_AXP2101_IRQ_VBUS_INSERT      (1UL << 15)
#define MT_AXP2101_IRQ_VBUS_REMOVE      (1UL << 14)
#define MT_AXP2101_IRQ_POWER_KEY_SHORT  (1UL << 11)
#define MT_AXP2101_IRQ_POWER_KEY_LONG   (1UL << 10)
#define MT_AXP2101_IRQ_CHARGE_START    (1UL << 19)
#define MT_AXP2101_IRQ_CHARGE_DONE     (1UL << 20)
#define MT_AXP2101_IRQ_DEFAULT \
    (MT_AXP2101_IRQ_BAT_INSERT | MT_AXP2101_IRQ_BAT_REMOVE | \
     MT_AXP2101_IRQ_VBUS_INSERT | MT_AXP2101_IRQ_VBUS_REMOVE | \
     MT_AXP2101_IRQ_POWER_KEY_SHORT | MT_AXP2101_IRQ_POWER_KEY_LONG | \
     MT_AXP2101_IRQ_CHARGE_START | MT_AXP2101_IRQ_CHARGE_DONE)

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

/** @brief Fill a profile with the schematic's default rail/charger settings. */
void mt_axp2101_profile_init_default(mt_axp2101_profile_t *profile);

/**
 * @brief Apply rail, charger, and IRQ settings to the PMU.
 *
 * @param device is the initialized PMU instance.
 * @param profile contains the requested settings.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t mt_axp2101_apply_profile(mt_axp2101_t *device,
                                   const mt_axp2101_profile_t *profile);

/**
 * @brief Read back one power rail's enable state and voltage setting.
 */
esp_err_t mt_axp2101_get_rail_info(mt_axp2101_t *device,
                                   mt_axp2101_rail_t rail,
                                   mt_axp2101_rail_info_t *info);

/** @brief Read the PMU interrupt status registers and clear them separately. */
esp_err_t mt_axp2101_get_irq_status(mt_axp2101_t *device, uint32_t *status);
esp_err_t mt_axp2101_clear_irq_status(mt_axp2101_t *device);
esp_err_t mt_axp2101_set_irq_mask(mt_axp2101_t *device, uint32_t mask);

#ifdef __cplusplus
}
#endif

#endif /* __MT_AXP2101_H__ */

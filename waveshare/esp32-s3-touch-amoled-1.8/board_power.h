#ifndef __BOARD_POWER_H__
#define __BOARD_POWER_H__

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "bsp_hal.h"
#include "mt_axp2101.h"
#include "board_tca9554.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the AXP2101 PMU and register its BSP operations.
 *
 * @param i2c_bus is the initialized board I2C bus.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_power_init(i2c_master_bus_handle_t i2c_bus);

/**
 * @brief Release the AXP2101 PMU instance.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_power_deinit(void);

/** @brief Report whether the AXP2101 instance is ready. */
bool board_power_is_available(void);

/** @brief Report whether PMU cleanup still owns an I2C device. */
bool board_power_has_resources(void);

/** @brief Read one board power telemetry snapshot. */
esp_err_t board_power_get_info(bsp_power_info_t *info);

/** @brief Apply the board's AXP2101 rail and charger profile. */
esp_err_t board_power_apply_profile(const mt_axp2101_profile_t *profile);

/** @brief Fill a profile with the schematic's default PMU settings. */
void board_power_profile_init_default(mt_axp2101_profile_t *profile);

/** @brief Read back one PMU rail's configured state. */
esp_err_t board_power_get_rail_info(mt_axp2101_rail_t rail,
                                    mt_axp2101_rail_info_t *info);

/** @brief Read and clear AXP2101 interrupt status registers. */
esp_err_t board_power_get_irq_status(uint32_t *status);
esp_err_t board_power_clear_irq_status(void);
esp_err_t board_power_set_irq_mask(uint32_t mask);

/**
 * @brief Return the underlying PMU instance for board-level integrations.
 *
 * The returned handle remains owned by the board and must not be destroyed by
 * the caller.
 */
mt_axp2101_t *board_power_get_pmu(void);

/** @brief Result of polling the active-low AXP IRQ through TCA9554. */
typedef struct board_power_irq_snapshot
{
    bool line_active;
    uint32_t status;
} board_power_irq_snapshot_t;

/**
 * @brief Poll the PMU IRQ input and consume pending AXP2101 status bits.
 *
 * @param io_expander is the TCA9554 carrying AXP_IRQ.
 * @param irq_pin_mask selects the active-low TCA input pin.
 * @param snapshot receives line and PMU status state.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_power_poll_irq(board_tca9554_t *io_expander,
                               uint8_t irq_pin_mask,
                               board_power_irq_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_POWER_H__ */

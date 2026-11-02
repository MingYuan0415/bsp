#ifndef __BOARD_INIT_H__
#define __BOARD_INIT_H__

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#include "bsp_hal.h"
#include "board_display_profile.h"

typedef struct board_sdmmc board_sdmmc_t;

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_I2C_PORT                    (I2C_NUM_0)
#define BOARD_I2C_PIN_SCL                 (GPIO_NUM_8)
#define BOARD_I2C_PIN_SDA                 (GPIO_NUM_7)
#define BOARD_I2C_CLK_HZ                  (400000)
#define BOARD_HOME_KEY_GPIO               (GPIO_NUM_35)
#define BOARD_POWER_KEY_GPIO              (GPIO_NUM_49)

/** @brief Recoverable LCD/panel lifecycle phase. */
typedef enum
{
    BOARD_DISPLAY_POWER_PHASE_OFF = 0,
    BOARD_DISPLAY_POWER_PHASE_PANEL_READY,
    BOARD_DISPLAY_POWER_PHASE_SCANOUT_ON,
    BOARD_DISPLAY_POWER_PHASE_ENABLED,
} board_display_power_phase_t;

/** @brief Display handles and recoverable hardware lifecycle state. */
typedef struct board_display_state
{
    bsp_display_port_t port;                 /**< LCD and touch handles. */
    uint8_t brightness;                      /**< Panel brightness target. */
    board_display_power_phase_t power_phase; /**< LCD lifecycle phase. */
    bool panel_initialized;                  /**< Panel init completion. */
    bool scanout_on;                         /**< Panel scan state. */
    bool brightness_applied;                 /**< Backlight duty applied. */
    bool enabled;                            /**< Visible panel state. */
    bool spi_bus_initialized;                /**< Owned SPI bus state. */
    bool touch_irq_enabled;                  /**< Touch interrupt state. */
    bool touch_configured;                   /**< Touch controller ready. */
} board_display_state_t;

/** @brief Persistent board settings. */
typedef struct board_settings
{
    uint8_t brightness; /**< User-selected brightness. */
} board_settings_t;

/** @brief Resources and committed capabilities owned by the board. */
typedef struct board_context
{
    bool initialized;                     /**< Board readiness state. */
    i2c_master_bus_handle_t i2c_bus;      /**< Owned I2C bus. */
    board_display_state_t display;        /**< Display state. */
    bool audio_initialized;               /**< ES8311 path state. */
    board_sdmmc_t *sd_card;               /**< Mounted SD card. */
    board_settings_t settings;            /**< Persistent settings. */
    bsp_capabilities_t capabilities;      /**< Ready capabilities. */
} board_context_t;

/**
 * @brief Report whether display and touch reached the committed screen-off state.
 *
 * @param display is the display lifecycle state to inspect.
 *
 * @return true when the screen is hidden, backlight is off and touch is quiet.
 *
 * @warning display must not be NULL.
 */
static inline bool _board_display_is_suspend_committed(
    const board_display_state_t *display)
{
    return display->power_phase == BOARD_DISPLAY_POWER_PHASE_OFF &&
           !display->scanout_on && !display->enabled &&
           !display->brightness_applied && !display->touch_irq_enabled;
}

/** @brief Fault-injection stages used by board initialization tests. */
typedef enum
{
    BOARD_INIT_STAGE_I2C = 0,
    BOARD_INIT_STAGE_INPUT,
    BOARD_INIT_STAGE_INPUT_TASK,
    BOARD_INIT_STAGE_DISPLAY,
    BOARD_INIT_STAGE_SPI_BUS,
    BOARD_INIT_STAGE_LCD_IO,
    BOARD_INIT_STAGE_LCD_PANEL,
    BOARD_INIT_STAGE_LCD_RESET,
    BOARD_INIT_STAGE_LCD_INIT,
    BOARD_INIT_STAGE_TOUCH_IO,
    BOARD_INIT_STAGE_TOUCH,
    BOARD_INIT_STAGE_POWER,
    BOARD_INIT_STAGE_AUDIO,
    BOARD_INIT_STAGE_SCREEN_OPS,
    BOARD_INIT_STAGE_DISPLAY_PORT,
} board_init_stage_t;

/**
 * @brief Initialize the selected board and commit its HAL registry.
 *
 * @return ESP_OK on success; otherwise the first initialization error.
 */
esp_err_t board_init(void);

/**
 * @brief Release board resources in reverse dependency order.
 *
 * @return ESP_OK on success; otherwise the first cleanup error.
 */
esp_err_t board_deinit(void);

/** @brief Return capabilities committed by the latest successful init. */
bsp_capabilities_t board_get_capabilities(void);

/**
 * @brief Copy the board light-sleep wake descriptor.
 *
 * This board has no RTC-capable key GPIO, so no CPU wake source is exported.
 */
bsp_wakeup_descriptor_t board_get_wakeup_descriptor(void);

/** @brief Return the owned I2C bus, or NULL before initialization. */
i2c_master_bus_handle_t board_get_i2c_bus(void);

/**
 * @brief Test-only fault injection hook.
 *
 * @param stage is the initialization stage about to run.
 *
 * @return ESP_OK to continue; otherwise the injected error.
 */
esp_err_t board_init_stage_gate(board_init_stage_t stage);

/** @brief Initialize the display, touch controller and backlight. */
esp_err_t board_display_init(board_context_t *board);

/** @brief Release the display, touch controller and backlight. */
esp_err_t board_display_deinit(board_context_t *board);

/** @brief Set the panel brightness and remember it as the display target. */
esp_err_t board_display_set_brightness_impl(board_context_t *board,
        uint8_t brightness);

/** @brief Apply one panel scan on/off transition without touching the backlight. */
esp_err_t board_display_set_power_impl(board_context_t *board, bool on);

/** @brief Show or hide the initialized panel and restore the backlight. */
esp_err_t board_display_set_enabled_impl(board_context_t *board, bool on);

/** @brief Commit the screen-off state used by app-manager standby. */
esp_err_t board_display_suspend_impl(board_context_t *board);

/** @brief Rebuild hidden screen hardware before the first visible frame. */
esp_err_t board_display_resume_prepare_impl(board_context_t *board);

/** @brief Show the panel, restore the backlight and enable touch IRQ. */
esp_err_t board_display_resume_commit_impl(board_context_t *board);

/** @brief Initialize the key input task. */
esp_err_t board_input_init(void);

/** @brief Join the key input task and release its resources. */
esp_err_t board_input_deinit(void);

/** @brief Register the single key event consumer. */
esp_err_t board_input_register_handler(bsp_input_cb_t callback,
                                       void *user_data);

/** @brief Remove the key event consumer and drain in-flight callbacks. */
esp_err_t board_input_unregister_handler(void);

/** @brief Pause key sampling for a system sleep handshake. */
esp_err_t board_input_prepare_sleep(uint32_t timeout_ms);

/** @brief Resume key sampling after a system sleep handshake. */
esp_err_t board_input_complete_sleep(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_INIT_H__ */

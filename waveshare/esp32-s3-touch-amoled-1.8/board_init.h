#ifndef __BOARD_INIT_H__
#define __BOARD_INIT_H__

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_io_expander.h"

#include "bsp_hal.h"
#include "board_display_profile.h"
#include "board_power.h"
#include "board_rtc.h"
#include "board_sdspi.h"
#include "board_tca9554.h"
#include "board_imu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_I2C_CLK_HZ                  (200000)
#define BOARD_I2C_PIN_INT                 (GPIO_NUM_21)
#define BOARD_I2C_PIN_RST                 (GPIO_NUM_NC)
#define BOARD_HOME_KEY_GPIO               (GPIO_NUM_0)
#define BOARD_EXIO_PIN_LCD_RESET          (IO_EXPANDER_PIN_NUM_0)
#define BOARD_EXIO_PIN_LCD_PWR_EN         (IO_EXPANDER_PIN_NUM_1)
#define BOARD_EXIO_PIN_TOUCH_RESET        (IO_EXPANDER_PIN_NUM_2)
#define BOARD_EXIO_PIN_RTC_INT            (IO_EXPANDER_PIN_NUM_3)
#define BOARD_EXIO_PIN_PWR_BUTTON         (IO_EXPANDER_PIN_NUM_4)
#define BOARD_EXIO_PIN_PMU_IRQ            (IO_EXPANDER_PIN_NUM_5)
#define BOARD_EXIO_PIN_IMU_INT            (IO_EXPANDER_PIN_NUM_6)
#define BOARD_EXIO_PIN_SD_CS              (IO_EXPANDER_PIN_NUM_7)

/** @brief Recoverable LCD rail and panel initialization phase. */
typedef enum
{
    BOARD_DISPLAY_POWER_PHASE_OFF = 0,
    BOARD_DISPLAY_POWER_PHASE_RAIL_ON,
    BOARD_DISPLAY_POWER_PHASE_RESET_ASSERTED,
    BOARD_DISPLAY_POWER_PHASE_RESET_RELEASED,
    BOARD_DISPLAY_POWER_PHASE_PANEL_RESET,
    BOARD_DISPLAY_POWER_PHASE_PANEL_INITIALIZED,
    BOARD_DISPLAY_POWER_PHASE_SCANOUT_HIDDEN,
    BOARD_DISPLAY_POWER_PHASE_ENABLED,
} board_display_power_phase_t;

/** @brief Combined display and touch suspend/resume phase. */
typedef enum
{
    BOARD_SCREEN_PHASE_ACTIVE = 0,
    BOARD_SCREEN_PHASE_SUSPENDING,
    BOARD_SCREEN_PHASE_SUSPENDED,
    BOARD_SCREEN_PHASE_TOUCH_RESET_ASSERTED,
    BOARD_SCREEN_PHASE_TOUCH_RESET_RELEASED,
    BOARD_SCREEN_PHASE_PANEL_PREPARED,
    BOARD_SCREEN_PHASE_TOUCH_CONFIGURED,
} board_screen_phase_t;

/** @brief Display handles and recoverable hardware lifecycle state. */
typedef struct board_display_state
{
    bsp_display_port_t port;                 /**< LCD and touch handles. */
    uint8_t brightness;                      /**< Target panel brightness. */
    board_display_power_phase_t power_phase; /**< LCD power phase. */
    bool rail_on;                            /**< LCD rail state. */
    bool reset_released;                     /**< LCD reset pin state. */
    bool panel_reset;                        /**< Panel reset completion. */
    bool panel_initialized;                  /**< Panel init completion. */
    bool scanout_on;                         /**< Physical panel scan state. */
    bool brightness_applied;                 /**< Brightness command state. */
    bool enabled;                            /**< Visible panel state. */
    bool spi_bus_initialized;                /**< Owned SPI bus state. */
    board_screen_phase_t screen_phase;       /**< Screen transaction phase. */
    bool touch_irq_enabled;                  /**< Physical touch IRQ state. */
    bool touch_hibernated;                   /**< Touch hibernate state. */
    bool touch_reset_released;               /**< Touch reset pin state. */
    bool touch_configured;                   /**< Touch config replay state. */
} board_display_state_t;

/**
 * @brief Report whether display and touch reached the complete rail-off state.
 *
 * @note Touch is quiescent after either controller hibernation or reset
 *       assertion. Keep suspend completion checks centralized here.
 *
 * @param display is the display lifecycle state to inspect.
 *
 * @return true when the suspend transaction is committed; false otherwise.
 *
 * @warning display must not be NULL.
 */
static inline bool _board_display_is_suspend_committed(
    const board_display_state_t *display)
{
    return display->screen_phase == BOARD_SCREEN_PHASE_SUSPENDED &&
           display->power_phase == BOARD_DISPLAY_POWER_PHASE_OFF &&
           !display->rail_on && !display->scanout_on && !display->enabled &&
           !display->touch_irq_enabled &&
           (display->touch_hibernated || !display->touch_reset_released);
}

/** @brief Persistent board settings. */
typedef struct board_settings
{
    uint8_t brightness; /**< User-selected brightness. */
} board_settings_t;

/** @brief Resources and committed capabilities owned by the board. */
typedef struct board_context
{
    bool initialized;                          /**< Board readiness state. */
    i2c_master_bus_handle_t i2c_bus;            /**< Owned I2C bus. */
    board_tca9554_t *io_expander_device;        /**< TCA9554 instance. */
    esp_io_expander_handle_t io_expander;       /**< TCA9554 base handle. */
    board_display_state_t display;              /**< Display state. */
    board_imu_t *imu;                            /**< QMI8658C instance. */
    bool audio_initialized;                      /**< ES8311 path state. */
    board_sdspi_t *sd_card;                      /**< Mounted SDSPI card. */
    board_settings_t settings;                  /**< Persistent settings. */
    bsp_capabilities_t capabilities;            /**< Ready capabilities. */
} board_context_t;

/**
 * @brief Report whether every TCA9554-dependent device released ownership.
 *
 * @param board is the board lifecycle state to inspect.
 * @param rtc_has_resources reports whether RTC cleanup retained hidden state.
 *
 * @return true when the TCA9554 can be released; false otherwise.
 *
 * @warning board must not be NULL.
 */
static inline bool _board_io_expander_dependents_released(
    const board_context_t *board, bool rtc_has_resources)
{
    return !rtc_has_resources && board->imu == NULL && board->sd_card == NULL &&
           board->display.port.panel == NULL &&
           board->display.port.panel_io == NULL &&
           board->display.port.touch == NULL &&
           board->display.port.touch_io == NULL;
}

/**
 * @brief Report whether every owned I2C device released the shared bus.
 *
 * @param board is the board lifecycle state to inspect.
 * @param rtc_has_resources reports whether RTC cleanup retained resources.
 * @param power_has_resources reports whether PMU cleanup retained resources.
 *
 * @return true when the I2C bus can be deleted; false otherwise.
 *
 * @warning board must not be NULL.
 */
static inline bool _board_i2c_dependents_released(
    const board_context_t *board, bool rtc_has_resources,
    bool power_has_resources)
{
    return board->io_expander_device == NULL && !rtc_has_resources &&
           !power_has_resources && board->imu == NULL &&
           !board->audio_initialized && board->sd_card == NULL &&
           board->display.port.touch == NULL &&
           board->display.port.touch_io == NULL;
}

/** @brief Fault-injection stages used by board initialization tests. */
typedef enum
{
    BOARD_INIT_STAGE_I2C = 0,
    BOARD_INIT_STAGE_IO_EXPANDER,
    BOARD_INIT_STAGE_IO_CONFIG,
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
    BOARD_INIT_STAGE_RTC,
    BOARD_INIT_STAGE_POWER,
    BOARD_INIT_STAGE_SCREEN_OPS,
    BOARD_INIT_STAGE_DISPLAY_PORT,
    BOARD_INIT_STAGE_IMU,
    BOARD_INIT_STAGE_AUDIO,
} board_init_stage_t;

/**
 * @brief Initialize required board resources and optional peripherals.
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

/**
 * @brief Return capabilities committed by the board initialization.
 *
 * @return Bitwise combination of BSP_CAPABILITY_* values.
 */
bsp_capabilities_t board_get_capabilities(void);

/**
 * @brief Return GPIO wake sources supported by this board.
 *
 * @return Light-sleep wake descriptor.
 */
bsp_wakeup_descriptor_t board_get_wakeup_descriptor(void);

/**
 * @brief Allow tests to reject a selected initialization stage.
 *
 * @param stage is the stage about to execute.
 *
 * @return ESP_OK by default; tests may return an injected error.
 */
esp_err_t board_init_stage_gate(board_init_stage_t stage);

/** @brief Return the shared board I2C bus while the board is initialized. */
i2c_master_bus_handle_t board_get_i2c_bus(void);

/** @brief Return the board TCA9554 handle while the board is initialized. */
board_tca9554_t *board_get_tca9554(void);

/**
 * @brief Initialize the LCD, touch controller, and owned SPI resources.
 *
 * @param board is the board context to update.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_display_init(board_context_t *board);

/**
 * @brief Release display, touch, and SPI resources.
 *
 * @param board is the board context to release.
 *
 * @return ESP_OK on success; otherwise the first cleanup error.
 */
esp_err_t board_display_deinit(board_context_t *board);

/**
 * @brief Apply or cache the display brightness.
 *
 * @param board is the active board context.
 * @param brightness is the 8-bit panel brightness value.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_display_set_brightness_impl(board_context_t *board,
        uint8_t brightness);

/**
 * @brief Show or hide an initialized panel.
 *
 * @param board is the active board context.
 * @param on selects visible or hidden state.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_display_set_enabled_impl(board_context_t *board, bool on);

/**
 * @brief Power the LCD hardware on or off.
 *
 * @param board is the active board context.
 * @param on selects powered or rail-off state.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_display_set_power_impl(board_context_t *board, bool on);

/**
 * @brief Hibernate touch and remove LCD rail power.
 *
 * @param board is the active board context.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_display_suspend_impl(board_context_t *board);

/**
 * @brief Rebuild LCD and touch while both remain hidden from users.
 *
 * @param board is the active board context.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_display_resume_prepare_impl(board_context_t *board);

/**
 * @brief Show the rebuilt LCD and enable the touch interrupt.
 *
 * @param board is the active board context.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_display_resume_commit_impl(board_context_t *board);

/**
 * @brief Start the board input sampling task.
 *
 * @param io_expander is the TCA9554 used for the power key.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_input_init(board_tca9554_t *io_expander);

/**
 * @brief Stop input sampling and release task synchronization resources.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_input_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_INIT_H__ */

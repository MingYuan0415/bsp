#ifndef __BSP_HAL_H__
#define __BSP_HAL_H__

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief LCD and touch handles exported to the UI adapter. */
typedef struct bsp_display_port
{
    uint16_t width;                        /**< Horizontal resolution. */
    uint16_t height;                       /**< Vertical resolution. */
    esp_lcd_panel_handle_t panel;          /**< LCD panel handle. */
    esp_lcd_panel_io_handle_t panel_io;    /**< LCD transport handle. */
    esp_lcd_touch_handle_t touch;          /**< Touch controller handle. */
    esp_lcd_panel_io_handle_t touch_io;    /**< Touch transport handle. */
} bsp_display_port_t;

/** @brief Display dimensions available without exposing device handles. */
typedef struct bsp_display_metrics
{
    uint16_t width;  /**< Horizontal resolution. */
    uint16_t height; /**< Vertical resolution. */
} bsp_display_metrics_t;

/** @brief Board power and charger telemetry snapshot. */
typedef struct bsp_power_info
{
    uint16_t battery_voltage_mv; /**< Battery millivolts. */
    int8_t battery_percent;      /**< Percent, or -1 if no battery. */
    bool is_charging;            /**< Charging state. */
    bool is_vbus_connected;      /**< VBUS presence. */
    bool is_discharging;         /**< Discharging state. */
    bool is_standby;             /**< PMU standby state. */
    bool is_vbus_good;           /**< Valid VBUS state. */
    uint16_t vbus_voltage_mv;    /**< VBUS millivolts. */
    uint16_t system_voltage_mv;  /**< System rail millivolts. */
} bsp_power_info_t;

/** @brief Transactional BSP lifecycle state. */
typedef enum
{
    BSP_INIT_STATE_UNINITIALIZED = 0,
    BSP_INIT_STATE_INITIALIZING,
    BSP_INIT_STATE_READY,
    BSP_INIT_STATE_DEINITIALIZING,
    BSP_INIT_STATE_FAILED,
} bsp_init_state_t;

/** @brief Bit mask of initialized board capabilities. */
typedef uint32_t bsp_capabilities_t;

/** @brief Individual board capability bits. */
enum
{
    BSP_CAPABILITY_NONE = 0,
    BSP_CAPABILITY_DISPLAY = (1U << 0),
    BSP_CAPABILITY_TOUCH = (1U << 1),
    BSP_CAPABILITY_INPUT = (1U << 2),
    BSP_CAPABILITY_RTC = (1U << 3),
    BSP_CAPABILITY_POWER = (1U << 4),
};

/** @brief Logical board keys. */
typedef enum
{
    BSP_KEY_HOME = 0,
    BSP_KEY_POWER,
    BSP_KEY_COUNT,
} bsp_key_t;

/** @brief Debounced board key events. */
typedef enum
{
    BSP_KEY_EVENT_DOWN = 0,
    BSP_KEY_EVENT_UP,
    BSP_KEY_EVENT_CLICK,
    BSP_KEY_EVENT_LONG_PRESS,
} bsp_key_event_t;

/** @brief GPIO wake sources usable by light sleep. */
typedef struct bsp_wakeup_descriptor
{
    uint64_t gpio_mask;       /**< GPIOs enabled as wake sources. */
    uint64_t active_low_mask; /**< Wake sources active at low level. */
} bsp_wakeup_descriptor_t;

/**
 * @brief Receive a debounced key event from the BSP input task.
 *
 * @param key is the logical key that changed.
 * @param event is the debounced key event.
 * @param user_data is the context supplied during registration.
 *
 * @warning Enqueue work and return promptly from this callback.
 */
typedef void (*bsp_input_cb_t)(bsp_key_t key, bsp_key_event_t event,
                               void *user_data);

/** @brief Board screen lifecycle and brightness operations. */
typedef struct bsp_screen_ops
{
    bool (*is_available)(void); /**< Report screen availability. */
    esp_err_t (*suspend)(void); /**< Hibernate touch and remove LCD power. */
    esp_err_t (*resume_prepare)(void); /**< Rebuild hidden screen hardware. */
    esp_err_t (*resume_commit)(void);  /**< Show screen and enable touch IRQ. */
    bool (*is_suspended)(void);        /**< Report non-visible screen state. */
    bool (*is_suspend_committed)(void); /**< Report complete rail-off state. */
    esp_err_t (*set_brightness)(uint8_t brightness); /**< Set and persist. */
    esp_err_t (*set_brightness_temp)(uint8_t brightness); /**< Set only. */
    uint8_t (*get_brightness)(void); /**< Return persistent brightness. */
    esp_err_t (*set_enabled)(bool on); /**< Show or hide initialized LCD. */
    esp_err_t (*set_power)(bool on); /**< Change LCD-only power state. */
} bsp_screen_ops_t;

/** @brief Board RTC operations. */
typedef struct bsp_rtc_ops
{
    bool (*is_available)(void);             /**< Report RTC availability. */
    esp_err_t (*read)(struct tm *timeinfo); /**< Read validated RTC time. */
    esp_err_t (*write)(const struct tm *timeinfo); /**< Write RTC time. */
} bsp_rtc_ops_t;

/** @brief Board PMU operations. */
typedef struct bsp_power_ops
{
    bool (*is_available)(void); /**< Report PMU availability. */
    esp_err_t (*get_info)(bsp_power_info_t *info); /**< Read telemetry. */
} bsp_power_ops_t;

/** @brief Board input registration and sleep-handshake operations. */
typedef struct bsp_input_ops
{
    esp_err_t (*register_handler)(bsp_input_cb_t cb,
                                  void *user_data); /**< Register consumer. */
    esp_err_t (*unregister_handler)(void); /**< Remove consumer and drain. */
    esp_err_t (*prepare_sleep)(uint32_t timeout_ms); /**< Pause sampling. */
    esp_err_t (*complete_sleep)(uint32_t timeout_ms); /**< Resume sampling. */
} bsp_input_ops_t;

/**
 * @brief Initialize the selected board and commit its HAL registry.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t bsp_init(void);

/**
 * @brief Release all board resources after consumers have stopped.
 *
 * @return ESP_OK on success; otherwise the first cleanup error.
 */
esp_err_t bsp_deinit(void);

/**
 * @brief Report whether the BSP transaction committed successfully.
 *
 * @return true when ready; false otherwise.
 */
bool bsp_is_initialized(void);

/**
 * @brief Return the current BSP lifecycle state.
 *
 * @return Current transactional lifecycle state.
 */
bsp_init_state_t bsp_get_init_state(void);

/**
 * @brief Return capabilities committed by the latest successful init.
 *
 * @return Capability mask when ready; BSP_CAPABILITY_NONE otherwise.
 */
bsp_capabilities_t bsp_get_capabilities(void);

/**
 * @brief Copy the board light-sleep wake descriptor.
 *
 * @param descriptor receives the committed wake sources.
 *
 * @return ESP_OK when ready; otherwise an ESP-IDF error.
 */
esp_err_t bsp_get_wakeup_descriptor(bsp_wakeup_descriptor_t *descriptor);

/**
 * @brief Return committed display dimensions.
 *
 * @return Display dimensions, or zero dimensions when not ready.
 */
bsp_display_metrics_t bsp_display_get_metrics(void);

/**
 * @brief Return the committed LCD and touch handles.
 *
 * @return Borrowed, immutable port pointer while the BSP state is
 *         BSP_INIT_STATE_READY; NULL otherwise.
 *
 * @warning This getter does not acquire a lifecycle lease. Stop all consumers
 *          before bsp_deinit() begins, and do not retain or use the pointer
 *          after deinitialization begins. Copying the port does not extend the
 *          lifetime of its LCD or touch handles.
 */
const bsp_display_port_t *bsp_display_get_port(void);

/**
 * @brief Copy a complete display port into the initializing registry.
 *
 * @param port is the complete board display port.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t bsp_display_set_port(const bsp_display_port_t *port);

/**
 * @brief Register complete screen operations during BSP initialization.
 *
 * @param ops is the operation table copied into the registry.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t bsp_hal_register_screen(const bsp_screen_ops_t *ops);

/**
 * @brief Register complete RTC operations during BSP initialization.
 *
 * @param ops is the operation table copied into the registry.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t bsp_hal_register_rtc(const bsp_rtc_ops_t *ops);

/**
 * @brief Register complete PMU operations during BSP initialization.
 *
 * @param ops is the operation table copied into the registry.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t bsp_hal_register_power(const bsp_power_ops_t *ops);

/**
 * @brief Register complete input operations during BSP initialization.
 *
 * @param ops is the operation table copied into the registry.
 *
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t bsp_hal_register_input(const bsp_input_ops_t *ops);

/**
 * @brief Return committed screen operations.
 *
 * @return Borrowed, immutable operation table while the BSP state is
 *         BSP_INIT_STATE_READY; NULL otherwise.
 *
 * @warning This getter does not acquire a lifecycle lease. Stop all consumers
 *          before bsp_deinit() begins, and do not retain or invoke the table
 *          after deinitialization begins.
 */
const bsp_screen_ops_t *bsp_hal_get_screen(void);

/**
 * @brief Return committed RTC operations.
 *
 * @return Borrowed, immutable operation table while the BSP state is
 *         BSP_INIT_STATE_READY; NULL otherwise.
 *
 * @warning This getter does not acquire a lifecycle lease. Stop all consumers
 *          before bsp_deinit() begins, and do not retain or invoke the table
 *          after deinitialization begins.
 */
const bsp_rtc_ops_t *bsp_hal_get_rtc(void);

/**
 * @brief Return committed PMU operations.
 *
 * @return Borrowed, immutable operation table while the BSP state is
 *         BSP_INIT_STATE_READY; NULL otherwise.
 *
 * @warning This getter does not acquire a lifecycle lease. Stop all consumers
 *          before bsp_deinit() begins, and do not retain or invoke the table
 *          after deinitialization begins.
 */
const bsp_power_ops_t *bsp_hal_get_power(void);

/**
 * @brief Return committed input operations.
 *
 * @return Borrowed, immutable operation table while the BSP state is
 *         BSP_INIT_STATE_READY; NULL otherwise.
 *
 * @warning This getter does not acquire a lifecycle lease. Stop all consumers
 *          before bsp_deinit() begins, and do not retain or invoke the table
 *          after deinitialization begins.
 */
const bsp_input_ops_t *bsp_hal_get_input(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_HAL_H__ */

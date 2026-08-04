/**
 * @brief  BSP HAL registry implementation
 * @note   Thread-safe registry with transactional board initialization.
 */

#include "bsp_hal.h"
#include "board_init.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static bsp_screen_ops_t s_screen_ops;
static bsp_rtc_ops_t    s_rtc_ops;
static bsp_power_ops_t  s_power_ops;
static bsp_input_ops_t  s_input_ops;
static bsp_imu_ops_t    s_imu_ops;
static bsp_audio_ops_t  s_audio_ops;
static bsp_sd_ops_t     s_sd_ops;

static bool s_screen_valid = false;
static bool s_rtc_valid    = false;
static bool s_power_valid  = false;
static bool s_input_valid  = false;
static bool s_imu_valid    = false;
static bool s_audio_valid  = false;
static bool s_sd_valid     = false;

static bsp_display_port_t s_display_port = {0};

static bsp_init_state_t s_init_state = BSP_INIT_STATE_UNINITIALIZED;
static bsp_capabilities_t s_capabilities = BSP_CAPABILITY_NONE;
static bsp_wakeup_descriptor_t s_wakeup_descriptor = {0};
static portMUX_TYPE s_registry_lock = portMUX_INITIALIZER_UNLOCKED;

#define BSP_REQUIRED_CAPABILITIES (BSP_CAPABILITY_DISPLAY | \
                                   BSP_CAPABILITY_TOUCH | \
                                   BSP_CAPABILITY_INPUT)

static void _bsp_hal_clear_registry(void)
{
    memset(&s_screen_ops, 0, sizeof(s_screen_ops));
    memset(&s_rtc_ops, 0, sizeof(s_rtc_ops));
    memset(&s_power_ops, 0, sizeof(s_power_ops));
    memset(&s_input_ops, 0, sizeof(s_input_ops));
    memset(&s_imu_ops, 0, sizeof(s_imu_ops));
    memset(&s_audio_ops, 0, sizeof(s_audio_ops));
    memset(&s_sd_ops, 0, sizeof(s_sd_ops));
    memset(&s_display_port, 0, sizeof(s_display_port));
    memset(&s_wakeup_descriptor, 0, sizeof(s_wakeup_descriptor));
    s_screen_valid = false;
    s_rtc_valid = false;
    s_power_valid = false;
    s_input_valid = false;
    s_imu_valid = false;
    s_audio_valid = false;
    s_sd_valid = false;
    s_capabilities = BSP_CAPABILITY_NONE;
}

static bool _bsp_hal_registration_allowed(void)
{
    return s_init_state == BSP_INIT_STATE_INITIALIZING;
}

esp_err_t bsp_init(void)
{
    esp_err_t result = ESP_OK;
    bool initialize = false;
    taskENTER_CRITICAL(&s_registry_lock);
    if (s_init_state == BSP_INIT_STATE_READY)
    {
        result = ESP_OK;
    }
    else if (s_init_state == BSP_INIT_STATE_INITIALIZING ||
             s_init_state == BSP_INIT_STATE_DEINITIALIZING)
    {
        result = ESP_ERR_INVALID_STATE;
    }
    else
    {
        _bsp_hal_clear_registry();
        s_init_state = BSP_INIT_STATE_INITIALIZING;
        initialize = true;
    }
    taskEXIT_CRITICAL(&s_registry_lock);
    if (!initialize)
    {
        return result;
    }

    result = board_init();
    bsp_capabilities_t capabilities = board_get_capabilities();
    if (result == ESP_OK &&
            (capabilities & BSP_REQUIRED_CAPABILITIES) != BSP_REQUIRED_CAPABILITIES)
    {
        result = ESP_ERR_INVALID_STATE;
    }

    if (result != ESP_OK)
    {
        (void)board_deinit();
        taskENTER_CRITICAL(&s_registry_lock);
        _bsp_hal_clear_registry();
        s_init_state = BSP_INIT_STATE_FAILED;
        taskEXIT_CRITICAL(&s_registry_lock);
        return result;
    }

    taskENTER_CRITICAL(&s_registry_lock);
    s_capabilities = capabilities;
    s_wakeup_descriptor = board_get_wakeup_descriptor();
    s_init_state = BSP_INIT_STATE_READY;
    taskEXIT_CRITICAL(&s_registry_lock);

    return result;
}

esp_err_t bsp_deinit(void)
{
    esp_err_t result = ESP_OK;
    bool deinitialize = false;
    taskENTER_CRITICAL(&s_registry_lock);
    if (s_init_state == BSP_INIT_STATE_UNINITIALIZED)
    {
        result = ESP_OK;
    }
    else if (s_init_state == BSP_INIT_STATE_INITIALIZING ||
             s_init_state == BSP_INIT_STATE_DEINITIALIZING)
    {
        result = ESP_ERR_INVALID_STATE;
    }
    else
    {
        s_init_state = BSP_INIT_STATE_DEINITIALIZING;
        deinitialize = true;
    }
    taskEXIT_CRITICAL(&s_registry_lock);
    if (!deinitialize)
    {
        return result;
    }

    result = board_deinit();

    taskENTER_CRITICAL(&s_registry_lock);
    _bsp_hal_clear_registry();
    s_init_state = result == ESP_OK ?
                   BSP_INIT_STATE_UNINITIALIZED : BSP_INIT_STATE_FAILED;
    taskEXIT_CRITICAL(&s_registry_lock);

    return result;
}

bool bsp_is_initialized(void)
{
    taskENTER_CRITICAL(&s_registry_lock);
    bool initialized = s_init_state == BSP_INIT_STATE_READY;
    taskEXIT_CRITICAL(&s_registry_lock);
    return initialized;
}

bsp_init_state_t bsp_get_init_state(void)
{
    taskENTER_CRITICAL(&s_registry_lock);
    bsp_init_state_t state = s_init_state;
    taskEXIT_CRITICAL(&s_registry_lock);
    return state;
}

bsp_capabilities_t bsp_get_capabilities(void)
{
    taskENTER_CRITICAL(&s_registry_lock);
    bsp_capabilities_t capabilities =
        (s_init_state == BSP_INIT_STATE_READY) ? s_capabilities : BSP_CAPABILITY_NONE;
    taskEXIT_CRITICAL(&s_registry_lock);
    return capabilities;
}

esp_err_t bsp_get_wakeup_descriptor(bsp_wakeup_descriptor_t *descriptor)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    bool lock_owned = false;
    if (descriptor == NULL)
    {
        return result;
    }
    taskENTER_CRITICAL(&s_registry_lock);
    lock_owned = true;
    if (s_init_state != BSP_INIT_STATE_READY)
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    *descriptor = s_wakeup_descriptor;
    result = ESP_OK;

exit:
    if (lock_owned)
    {
        taskEXIT_CRITICAL(&s_registry_lock);
    }
    return result;
}

bsp_display_metrics_t bsp_display_get_metrics(void)
{
    bsp_display_metrics_t metrics = {0};
    taskENTER_CRITICAL(&s_registry_lock);
    if (s_init_state == BSP_INIT_STATE_READY)
    {
        metrics.width = s_display_port.width;
        metrics.height = s_display_port.height;
    }
    taskEXIT_CRITICAL(&s_registry_lock);
    return metrics;
}

const bsp_display_port_t *bsp_display_get_port(void)
{
    const bsp_display_port_t *port = NULL;
    taskENTER_CRITICAL(&s_registry_lock);
    if (s_init_state == BSP_INIT_STATE_READY &&
            s_display_port.panel != NULL &&
            s_display_port.panel_io != NULL &&
            s_display_port.touch != NULL &&
            s_display_port.touch_io != NULL)
    {
        port = &s_display_port;
    }
    taskEXIT_CRITICAL(&s_registry_lock);
    return port;
}

esp_err_t bsp_display_set_port(const bsp_display_port_t *port)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    bool lock_owned = false;
    if (port == NULL || port->width == 0 || port->height == 0 ||
            port->panel == NULL || port->panel_io == NULL ||
            port->touch == NULL || port->touch_io == NULL ||
            port->transport.kind == BSP_DISPLAY_TRANSPORT_UNKNOWN ||
            port->transport.clock_hz == 0U ||
            port->transport.max_transfer_lines == 0U ||
            port->transport.max_transfer_lines > port->height ||
            port->transport.dma_max_full_lines == 0U ||
            port->transport.transaction_queue_depth == 0U ||
            port->transport.data_lines == 0U ||
            port->transport.bits_per_pixel == 0U ||
            (port->te.enabled && port->te.gpio_num < 0))
    {
        return result;
    }

    taskENTER_CRITICAL(&s_registry_lock);
    lock_owned = true;
    if (!_bsp_hal_registration_allowed())
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    memcpy(&s_display_port, port, sizeof(bsp_display_port_t));
    result = ESP_OK;

exit:
    if (lock_owned)
    {
        taskEXIT_CRITICAL(&s_registry_lock);
    }
    return result;
}

esp_err_t bsp_hal_register_screen(const bsp_screen_ops_t *ops)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    bool lock_owned = false;
    if (!ops || !ops->is_available || !ops->suspend ||
            !ops->resume_prepare || !ops->resume_commit ||
            !ops->is_suspended || !ops->is_suspend_committed ||
            !ops->set_brightness ||
            !ops->set_brightness_temp || !ops->get_brightness ||
            !ops->set_enabled || !ops->set_power)
    {
        return result;
    }
    taskENTER_CRITICAL(&s_registry_lock);
    lock_owned = true;
    if (!_bsp_hal_registration_allowed())
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    memcpy(&s_screen_ops, ops, sizeof(bsp_screen_ops_t));
    s_screen_valid = true;
    result = ESP_OK;

exit:
    if (lock_owned)
    {
        taskEXIT_CRITICAL(&s_registry_lock);
    }
    return result;
}

const bsp_screen_ops_t *bsp_hal_get_screen(void)
{
    taskENTER_CRITICAL(&s_registry_lock);
    const bsp_screen_ops_t *ops =
        (s_init_state == BSP_INIT_STATE_READY && s_screen_valid) ? &s_screen_ops : NULL;
    taskEXIT_CRITICAL(&s_registry_lock);
    return ops;
}

esp_err_t bsp_hal_register_rtc(const bsp_rtc_ops_t *ops)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    bool lock_owned = false;
    if (ops == NULL || ops->is_available == NULL || ops->read == NULL ||
            ops->write == NULL || ops->alarm_configure == NULL ||
            ops->alarm_disable == NULL || ops->alarm_get_status == NULL ||
            ops->alarm_clear == NULL || ops->alarm_poll_interrupt == NULL)
    {
        return result;
    }
    taskENTER_CRITICAL(&s_registry_lock);
    lock_owned = true;
    if (!_bsp_hal_registration_allowed())
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    memcpy(&s_rtc_ops, ops, sizeof(bsp_rtc_ops_t));
    s_rtc_valid = true;
    result = ESP_OK;

exit:
    if (lock_owned)
    {
        taskEXIT_CRITICAL(&s_registry_lock);
    }
    return result;
}

const bsp_rtc_ops_t *bsp_hal_get_rtc(void)
{
    taskENTER_CRITICAL(&s_registry_lock);
    const bsp_rtc_ops_t *ops =
        (s_init_state == BSP_INIT_STATE_READY && s_rtc_valid) ? &s_rtc_ops : NULL;
    taskEXIT_CRITICAL(&s_registry_lock);
    return ops;
}

esp_err_t bsp_hal_register_power(const bsp_power_ops_t *ops)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    bool lock_owned = false;
    if (ops == NULL || ops->is_available == NULL || ops->get_info == NULL ||
            ops->poll_irq == NULL)
    {
        return result;
    }
    taskENTER_CRITICAL(&s_registry_lock);
    lock_owned = true;
    if (!_bsp_hal_registration_allowed())
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    memcpy(&s_power_ops, ops, sizeof(bsp_power_ops_t));
    s_power_valid = true;
    result = ESP_OK;

exit:
    if (lock_owned)
    {
        taskEXIT_CRITICAL(&s_registry_lock);
    }
    return result;
}

const bsp_power_ops_t *bsp_hal_get_power(void)
{
    taskENTER_CRITICAL(&s_registry_lock);
    const bsp_power_ops_t *ops =
        (s_init_state == BSP_INIT_STATE_READY && s_power_valid) ? &s_power_ops : NULL;
    taskEXIT_CRITICAL(&s_registry_lock);
    return ops;
}

esp_err_t bsp_hal_register_imu(const bsp_imu_ops_t *ops)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    bool lock_owned = false;
    if (ops == NULL || ops->is_available == NULL || ops->configure == NULL ||
            ops->read == NULL || ops->set_enabled == NULL ||
            ops->get_data_ready == NULL ||
            ops->get_interrupt_level == NULL)
    {
        return result;
    }

    taskENTER_CRITICAL(&s_registry_lock);
    lock_owned = true;
    if (!_bsp_hal_registration_allowed())
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    memcpy(&s_imu_ops, ops, sizeof(s_imu_ops));
    s_imu_valid = true;
    result = ESP_OK;

exit:
    if (lock_owned)
    {
        taskEXIT_CRITICAL(&s_registry_lock);
    }
    return result;
}

const bsp_imu_ops_t *bsp_hal_get_imu(void)
{
    taskENTER_CRITICAL(&s_registry_lock);
    const bsp_imu_ops_t *ops =
        (s_init_state == BSP_INIT_STATE_READY && s_imu_valid) ? &s_imu_ops : NULL;
    taskEXIT_CRITICAL(&s_registry_lock);
    return ops;
}

esp_err_t bsp_hal_register_audio(const bsp_audio_ops_t *ops)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    bool lock_owned = false;
    if (ops == NULL || ops->is_available == NULL || ops->configure == NULL ||
            ops->start == NULL || ops->stop == NULL || ops->write == NULL ||
            ops->read == NULL || ops->set_volume == NULL ||
            ops->get_volume == NULL || ops->set_mute == NULL ||
            ops->get_mute == NULL || ops->set_pa == NULL)
    {
        return result;
    }

    taskENTER_CRITICAL(&s_registry_lock);
    lock_owned = true;
    if (!_bsp_hal_registration_allowed())
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    memcpy(&s_audio_ops, ops, sizeof(s_audio_ops));
    s_audio_valid = true;
    result = ESP_OK;

exit:
    if (lock_owned)
    {
        taskEXIT_CRITICAL(&s_registry_lock);
    }
    return result;
}

const bsp_audio_ops_t *bsp_hal_get_audio(void)
{
    taskENTER_CRITICAL(&s_registry_lock);
    const bsp_audio_ops_t *ops =
        (s_init_state == BSP_INIT_STATE_READY && s_audio_valid) ? &s_audio_ops : NULL;
    taskEXIT_CRITICAL(&s_registry_lock);
    return ops;
}

esp_err_t bsp_hal_register_sd(const bsp_sd_ops_t *ops)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    bool lock_owned = false;
    if (ops == NULL || ops->is_available == NULL || ops->mount == NULL ||
            ops->unmount == NULL || ops->is_mounted == NULL ||
            ops->get_mount_point == NULL)
    {
        return result;
    }

    taskENTER_CRITICAL(&s_registry_lock);
    lock_owned = true;
    if (!_bsp_hal_registration_allowed())
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    memcpy(&s_sd_ops, ops, sizeof(s_sd_ops));
    s_sd_valid = true;
    result = ESP_OK;

exit:
    if (lock_owned)
    {
        taskEXIT_CRITICAL(&s_registry_lock);
    }
    return result;
}

const bsp_sd_ops_t *bsp_hal_get_sd(void)
{
    taskENTER_CRITICAL(&s_registry_lock);
    const bsp_sd_ops_t *ops =
        (s_init_state == BSP_INIT_STATE_READY && s_sd_valid) ? &s_sd_ops : NULL;
    taskEXIT_CRITICAL(&s_registry_lock);
    return ops;
}

esp_err_t bsp_hal_register_input(const bsp_input_ops_t *ops)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    bool lock_owned = false;
    if (!ops || !ops->register_handler || !ops->unregister_handler ||
            !ops->prepare_sleep || !ops->complete_sleep)
    {
        return result;
    }
    taskENTER_CRITICAL(&s_registry_lock);
    lock_owned = true;
    if (!_bsp_hal_registration_allowed())
    {
        result = ESP_ERR_INVALID_STATE;
        goto exit;
    }
    memcpy(&s_input_ops, ops, sizeof(bsp_input_ops_t));
    s_input_valid = true;
    result = ESP_OK;

exit:
    if (lock_owned)
    {
        taskEXIT_CRITICAL(&s_registry_lock);
    }
    return result;
}

const bsp_input_ops_t *bsp_hal_get_input(void)
{
    taskENTER_CRITICAL(&s_registry_lock);
    const bsp_input_ops_t *ops =
        (s_init_state == BSP_INIT_STATE_READY && s_input_valid) ? &s_input_ops : NULL;
    taskEXIT_CRITICAL(&s_registry_lock);
    return ops;
}

/**
 * @brief Board input sampling and typed event dispatch
 * @note  All hardware reads and consumer callbacks run in _board_input_task.
 */

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_err.h"

#include "board_init.h"
#include "board_tca9554.h"
#include "bsp_hal.h"

#define DBG_TAG "board_input"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#define BOARD_INPUT_HOME_GPIO             (GPIO_NUM_0)
#define BOARD_INPUT_HOME_ACTIVE_LEVEL     (0)
#define BOARD_INPUT_POWER_ACTIVE_LEVEL    (1)
#define BOARD_INPUT_POLL_MS               (20U)
#define BOARD_INPUT_ERROR_BACKOFF_MS      (1000U)
#define BOARD_INPUT_ERROR_LOG_MS          (10000U)
#define BOARD_INPUT_DEBOUNCE_SAMPLES      (3U)
#define BOARD_INPUT_LONG_PRESS_MS         (1000U)
#define BOARD_INPUT_TASK_STACK_SIZE       (3072U)
#define BOARD_INPUT_TASK_PRIORITY         (5U)
#define BOARD_INPUT_STOP_TIMEOUT_MS       (200U)

typedef struct board_key_state
{
    bool initialized;
    bool stable_active;
    bool candidate_active;
    bool event_sequence_started;
    bool long_press_sent;
    uint8_t candidate_samples;
    TickType_t pressed_at;
} board_key_state_t;

typedef struct board_input_poll_state
{
    TickType_t next_power_sample;
    TickType_t last_error_log;
    bool error_was_logged;
    bool power_sample_failed;
} board_input_poll_state_t;

static board_tca9554_t *s_io_expander = NULL;
static TaskHandle_t s_input_task = NULL;
static SemaphoreHandle_t s_task_stopped = NULL;
static SemaphoreHandle_t s_task_paused = NULL;
static SemaphoreHandle_t s_task_resumed = NULL;
static atomic_bool s_stop_requested;
static atomic_bool s_pause_requested;
static atomic_uint s_pause_generation;
static atomic_uint s_pause_request_generation;
static atomic_uint s_paused_generation;
static atomic_uint s_resumed_generation;
static atomic_bool s_stop_tail_complete = ATOMIC_VAR_INIT(true);
static bool s_initialized = false;
static bool s_quiesced = false;
static bool s_resume_pending = false;
static unsigned s_quiesced_generation = 0;

static bsp_input_cb_t s_input_cb = NULL;
static void *s_input_user_data = NULL;
static uint32_t s_callbacks_active = 0;
static portMUX_TYPE s_handler_lock = portMUX_INITIALIZER_UNLOCKED;

static board_key_state_t s_home_state;
static board_key_state_t s_power_state;

static unsigned _board_input_next_pause_generation(void)
{
    unsigned generation = atomic_fetch_add(&s_pause_generation, 1U) + 1U;
    if (generation == 0)
    {
        generation = atomic_fetch_add(&s_pause_generation, 1U) + 1U;
    }
    return generation;
}

static bool _board_input_wait_for_generation(SemaphoreHandle_t semaphore,
        atomic_uint *ack_generation,
        unsigned expected_generation,
        uint32_t timeout_ms)
{
    bool acknowledged = false;
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    if (timeout_ticks == 0)
    {
        timeout_ticks = 1;
    }
    const TickType_t started_at = xTaskGetTickCount();

    while (!acknowledged)
    {
        const TickType_t elapsed = xTaskGetTickCount() - started_at;
        if (elapsed >= timeout_ticks)
        {
            break;
        }

        const TickType_t remaining = timeout_ticks - elapsed;
        if (xSemaphoreTake(semaphore, remaining) != pdTRUE)
        {
            break;
        }
        acknowledged = atomic_load(ack_generation) == expected_generation;
    }
    return acknowledged;
}

static void _board_input_notify(bsp_key_t key, bsp_key_event_t event)
{
    bsp_input_cb_t callback = NULL;
    void *user_data = NULL;

    taskENTER_CRITICAL(&s_handler_lock);
    callback = s_input_cb;
    user_data = s_input_user_data;
    if (callback != NULL)
    {
        s_callbacks_active++;
    }
    taskEXIT_CRITICAL(&s_handler_lock);

    if (callback != NULL)
    {
        callback(key, event, user_data);
        taskENTER_CRITICAL(&s_handler_lock);
        s_callbacks_active--;
        taskEXIT_CRITICAL(&s_handler_lock);
    }
}

static void _board_input_update_key(board_key_state_t *state,
                                    bsp_key_t key,
                                    bool active,
                                    TickType_t now)
{
    if (!state->initialized)
    {
        state->initialized = true;
        state->stable_active = active;
        state->candidate_active = active;
        state->pressed_at = now;
        state->event_sequence_started = false;
    }
    else
    {
        if (active != state->candidate_active)
        {
            state->candidate_active = active;
            state->candidate_samples = 1;
        }
        else if (state->candidate_samples < BOARD_INPUT_DEBOUNCE_SAMPLES)
        {
            state->candidate_samples++;
        }

        if (state->candidate_samples >= BOARD_INPUT_DEBOUNCE_SAMPLES &&
                state->stable_active != state->candidate_active)
        {
            state->stable_active = state->candidate_active;
            if (state->stable_active)
            {
                state->pressed_at = now;
                state->event_sequence_started = true;
                state->long_press_sent = false;
                _board_input_notify(key, BSP_KEY_EVENT_DOWN);
            }
            else if (state->event_sequence_started)
            {
                _board_input_notify(key, BSP_KEY_EVENT_UP);
                if (!state->long_press_sent)
                {
                    _board_input_notify(key, BSP_KEY_EVENT_CLICK);
                }
                state->event_sequence_started = false;
            }
        }

        if (state->stable_active && state->event_sequence_started &&
                !state->long_press_sent &&
                (now - state->pressed_at) >=
                pdMS_TO_TICKS(BOARD_INPUT_LONG_PRESS_MS))
        {
            state->long_press_sent = true;
            _board_input_notify(key, BSP_KEY_EVENT_LONG_PRESS);
        }
    }
}

static bool _board_input_handle_pause(void)
{
    bool handled = atomic_load(&s_pause_requested);
    if (handled)
    {
        const unsigned generation = atomic_load(&s_pause_request_generation);
        atomic_store(&s_paused_generation, generation);
        (void)xSemaphoreGive(s_task_paused);
        while (atomic_load(&s_pause_requested) &&
                atomic_load(&s_pause_request_generation) == generation &&
                !atomic_load(&s_stop_requested))
        {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }

        if (!atomic_load(&s_stop_requested))
        {
            memset(&s_home_state, 0, sizeof(s_home_state));
            memset(&s_power_state, 0, sizeof(s_power_state));
            atomic_store(&s_resumed_generation, generation);
            (void)xSemaphoreGive(s_task_resumed);
        }
    }
    return handled;
}

static void _board_input_sample_power(board_input_poll_state_t *poll,
                                      TickType_t now)
{
    if ((int32_t)(now - poll->next_power_sample) >= 0)
    {
        uint8_t levels = 0;
        const esp_err_t result = board_tca9554_read_inputs(
                                     s_io_expander, &levels);
        if (result == ESP_OK)
        {
            const bool power_active =
                ((levels & BOARD_EXIO_PIN_PWR_BUTTON) != 0) ==
                BOARD_INPUT_POWER_ACTIVE_LEVEL;
            _board_input_update_key(&s_power_state, BSP_KEY_POWER,
                                    power_active, now);
            if (poll->power_sample_failed)
            {
                LOG_I("power key sampling recovered");
            }
            poll->power_sample_failed = false;
            poll->error_was_logged = false;
            poll->next_power_sample =
                now + pdMS_TO_TICKS(BOARD_INPUT_POLL_MS);
        }
        else
        {
            poll->power_sample_failed = true;
            poll->next_power_sample =
                now + pdMS_TO_TICKS(BOARD_INPUT_ERROR_BACKOFF_MS);
            if (!poll->error_was_logged ||
                    (now - poll->last_error_log) >=
                    pdMS_TO_TICKS(BOARD_INPUT_ERROR_LOG_MS))
            {
                LOG_W("power key sample failed, backing off: %s",
                      esp_err_to_name(result));
                poll->last_error_log = now;
                poll->error_was_logged = true;
            }
        }
    }
}

static void _board_input_sample_keys(board_input_poll_state_t *poll)
{
    const TickType_t now = xTaskGetTickCount();
    const bool home_active =
        gpio_get_level(BOARD_INPUT_HOME_GPIO) ==
        BOARD_INPUT_HOME_ACTIVE_LEVEL;
    _board_input_update_key(&s_home_state, BSP_KEY_HOME, home_active, now);
    _board_input_sample_power(poll, now);
}

static void _board_input_task(void *argument)
{
    (void)argument;
    board_input_poll_state_t poll = {0};

    while (!atomic_load(&s_stop_requested))
    {
        if (!_board_input_handle_pause())
        {
            _board_input_sample_keys(&poll);
            (void)ulTaskNotifyTake(pdTRUE,
                                   pdMS_TO_TICKS(BOARD_INPUT_POLL_MS));
        }
    }

    (void)xSemaphoreGive(s_task_stopped);
    atomic_store_explicit(&s_stop_tail_complete, true, memory_order_release);
    vTaskDelete(NULL);
}

static esp_err_t _board_input_register_handler(bsp_input_cb_t callback,
        void *user_data)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (callback == NULL)
    {
        return result;
    }

    taskENTER_CRITICAL(&s_handler_lock);
    if (s_input_cb != NULL || s_callbacks_active != 0)
    {
        result = ESP_ERR_INVALID_STATE;
    }
    else
    {
        s_input_cb = callback;
        s_input_user_data = user_data;
        result = ESP_OK;
    }
    taskEXIT_CRITICAL(&s_handler_lock);
    return result;
}

static esp_err_t _board_input_unregister_handler(void)
{
    esp_err_t result = ESP_OK;
    const bool called_from_input_task =
        xTaskGetCurrentTaskHandle() == s_input_task;
    taskENTER_CRITICAL(&s_handler_lock);
    uint32_t callbacks_active = s_callbacks_active;
    if (called_from_input_task && callbacks_active != 0)
    {
        result = ESP_ERR_INVALID_STATE;
    }
    else
    {
        s_input_cb = NULL;
        s_input_user_data = NULL;
    }
    taskEXIT_CRITICAL(&s_handler_lock);

    if (result != ESP_OK)
    {
        return result;
    }
    while (callbacks_active != 0)
    {
        vTaskDelay(1);
        taskENTER_CRITICAL(&s_handler_lock);
        callbacks_active = s_callbacks_active;
        taskEXIT_CRITICAL(&s_handler_lock);
    }
    return result;
}

static esp_err_t _board_input_prepare_sleep(uint32_t timeout_ms)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (!s_initialized || s_input_task == NULL || timeout_ms == 0 ||
            xTaskGetCurrentTaskHandle() == s_input_task)
    {
        return result;
    }

    result = ESP_OK;
    if (s_quiesced)
    {
        if (!s_resume_pending)
        {
            return result;
        }
        if (atomic_load(&s_resumed_generation) != s_quiesced_generation &&
                !_board_input_wait_for_generation(s_task_resumed,
                        &s_resumed_generation,
                        s_quiesced_generation,
                        timeout_ms))
        {
            return ESP_ERR_TIMEOUT;
        }
        s_quiesced = false;
        s_resume_pending = false;
    }

    (void)xSemaphoreTake(s_task_paused, 0);
    (void)xSemaphoreTake(s_task_resumed, 0);
    const unsigned generation = _board_input_next_pause_generation();
    atomic_store(&s_pause_request_generation, generation);
    atomic_store(&s_pause_requested, true);
    xTaskNotifyGive(s_input_task);
    if (!_board_input_wait_for_generation(s_task_paused,
                                          &s_paused_generation,
                                          generation,
                                          timeout_ms))
    {
        atomic_store(&s_pause_requested, false);
        xTaskNotifyGive(s_input_task);
        return ESP_ERR_TIMEOUT;
    }

    s_quiesced = true;
    s_quiesced_generation = generation;
    return result;
}

static esp_err_t _board_input_complete_sleep(uint32_t timeout_ms)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (!s_initialized || s_input_task == NULL || timeout_ms == 0 ||
            xTaskGetCurrentTaskHandle() == s_input_task)
    {
        return result;
    }

    result = ESP_OK;
    if (!s_quiesced)
    {
        return result;
    }

    const unsigned generation = s_quiesced_generation;
    if (atomic_load(&s_resumed_generation) == generation)
    {
        s_quiesced = false;
        s_resume_pending = false;
        return result;
    }
    if (!s_resume_pending)
    {
        atomic_store(&s_pause_requested, false);
        s_resume_pending = true;
        xTaskNotifyGive(s_input_task);
    }
    if (!_board_input_wait_for_generation(s_task_resumed,
                                          &s_resumed_generation,
                                          generation,
                                          timeout_ms))
    {
        return ESP_ERR_TIMEOUT;
    }

    s_quiesced = false;
    s_resume_pending = false;
    return result;
}

static const bsp_input_ops_t s_input_ops =
{
    .register_handler = _board_input_register_handler,
    .unregister_handler = _board_input_unregister_handler,
    .prepare_sleep = _board_input_prepare_sleep,
    .complete_sleep = _board_input_complete_sleep,
};

static void _board_input_delete_sync_objects(void)
{
    if (s_task_stopped != NULL)
    {
        vSemaphoreDelete(s_task_stopped);
        s_task_stopped = NULL;
    }
    if (s_task_paused != NULL)
    {
        vSemaphoreDelete(s_task_paused);
        s_task_paused = NULL;
    }
    if (s_task_resumed != NULL)
    {
        vSemaphoreDelete(s_task_resumed);
        s_task_resumed = NULL;
    }
}

static esp_err_t _board_input_create_sync_objects(void)
{
    esp_err_t result = ESP_OK;
    s_task_stopped = xSemaphoreCreateBinary();
    s_task_paused = xSemaphoreCreateBinary();
    s_task_resumed = xSemaphoreCreateBinary();
    if (s_task_stopped == NULL || s_task_paused == NULL ||
            s_task_resumed == NULL)
    {
        _board_input_delete_sync_objects();
        result = ESP_ERR_NO_MEM;
    }
    return result;
}

static void _board_input_prepare_runtime_state(
    board_tca9554_t *io_expander)
{
    s_io_expander = io_expander;
    s_input_task = NULL;
    atomic_store(&s_stop_requested, false);
    atomic_store(&s_pause_requested, false);
    atomic_store(&s_pause_generation, 0);
    atomic_store(&s_pause_request_generation, 0);
    atomic_store(&s_paused_generation, 0);
    atomic_store(&s_resumed_generation, 0);
    s_quiesced = false;
    s_resume_pending = false;
    s_quiesced_generation = 0;
    atomic_store_explicit(&s_stop_tail_complete, false,
                          memory_order_release);
    taskENTER_CRITICAL(&s_handler_lock);
    s_callbacks_active = 0;
    taskEXIT_CRITICAL(&s_handler_lock);
    memset(&s_home_state, 0, sizeof(s_home_state));
    memset(&s_power_state, 0, sizeof(s_power_state));
}

static void _board_input_clear_runtime_state(void)
{
    s_io_expander = NULL;
    s_input_task = NULL;
    atomic_store(&s_stop_requested, false);
    atomic_store(&s_pause_requested, false);
    atomic_store(&s_pause_generation, 0);
    atomic_store(&s_pause_request_generation, 0);
    atomic_store(&s_paused_generation, 0);
    atomic_store(&s_resumed_generation, 0);
    atomic_store_explicit(&s_stop_tail_complete, true,
                          memory_order_release);
    s_quiesced = false;
    s_resume_pending = false;
    s_quiesced_generation = 0;
    s_initialized = false;
    taskENTER_CRITICAL(&s_handler_lock);
    s_callbacks_active = 0;
    taskEXIT_CRITICAL(&s_handler_lock);
    memset(&s_home_state, 0, sizeof(s_home_state));
    memset(&s_power_state, 0, sizeof(s_power_state));
}

static esp_err_t _board_input_create_task(void)
{
    const BaseType_t task_result = xTaskCreate(
                                       _board_input_task,
                                       "board_input",
                                       BOARD_INPUT_TASK_STACK_SIZE,
                                       NULL,
                                       BOARD_INPUT_TASK_PRIORITY,
                                       &s_input_task);
    esp_err_t result = ESP_OK;
    if (task_result != pdPASS)
    {
        atomic_store_explicit(&s_stop_tail_complete, true,
                              memory_order_release);
        s_input_task = NULL;
        result = ESP_ERR_NO_MEM;
    }
    return result;
}

static esp_err_t _board_input_stop_task(void)
{
    esp_err_t result = ESP_OK;
    if (s_input_task != NULL)
    {
        if (xSemaphoreTake(s_task_stopped, 0) != pdTRUE)
        {
            atomic_store(&s_stop_requested, true);
            atomic_store(&s_pause_requested, false);
            s_quiesced = false;
            xTaskNotifyGive(s_input_task);
            if (xSemaphoreTake(
                        s_task_stopped,
                        pdMS_TO_TICKS(BOARD_INPUT_STOP_TIMEOUT_MS)) != pdTRUE)
            {
                result = ESP_ERR_TIMEOUT;
            }
        }

        if (result == ESP_OK)
        {
            while (!atomic_load_explicit(&s_stop_tail_complete,
                                         memory_order_acquire))
            {
                vTaskDelay(1);
            }
            s_input_task = NULL;
        }
    }
    return result;
}

esp_err_t board_input_init(board_tca9554_t *io_expander)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (io_expander == NULL)
    {
        return result;
    }
    if (s_initialized)
    {
        return ESP_OK;
    }
    if (s_input_task != NULL || s_task_stopped != NULL ||
            s_task_paused != NULL || s_task_resumed != NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    const gpio_config_t home_config =
    {
        .pin_bit_mask = (1ULL << BOARD_INPUT_HOME_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    result = gpio_config(&home_config);
    if (result != ESP_OK)
    {
        return result;
    }

    result = board_init_stage_gate(BOARD_INIT_STAGE_INPUT_TASK);
    if (result == ESP_OK)
    {
        result = _board_input_create_sync_objects();
    }
    if (result == ESP_OK)
    {
        _board_input_prepare_runtime_state(io_expander);
        result = _board_input_create_task();
    }

    if (result == ESP_OK)
    {
        result = bsp_hal_register_input(&s_input_ops);
    }
    if (result != ESP_OK)
    {
        if (s_input_task != NULL)
        {
            (void)board_input_deinit();
        }
        else
        {
            _board_input_delete_sync_objects();
            _board_input_clear_runtime_state();
            (void)gpio_reset_pin(BOARD_INPUT_HOME_GPIO);
        }
    }
    else
    {
        s_initialized = true;
        LOG_I("input task ready: HOME(GPIO0) + POWER(TCA9554)");
    }
    return result;
}

esp_err_t board_input_deinit(void)
{
    esp_err_t result = ESP_OK;
    if (s_input_task != NULL && xTaskGetCurrentTaskHandle() == s_input_task)
    {
        return ESP_ERR_INVALID_STATE;
    }

    result = _board_input_unregister_handler();
    if (result != ESP_OK)
    {
        return result;
    }

    result = _board_input_stop_task();
    if (result != ESP_OK)
    {
        return result;
    }

    _board_input_delete_sync_objects();
    result = gpio_reset_pin(BOARD_INPUT_HOME_GPIO);
    _board_input_clear_runtime_state();
    return result;
}

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#if CONFIG_MAIN_PROJECT_TASK_AFFINITY_CPU0 || \
    CONFIG_MAIN_PROJECT_TASK_AFFINITY_CPU1
    #include "freertos/idf_additions.h"
#endif
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_err.h"

#include "board_init.h"
#include "bsp_hal.h"

#define DBG_TAG "board_input"
#include "mt_log.h"

#define BOARD_INPUT_POLL_MS              (20U)
#define BOARD_INPUT_DEBOUNCE_SAMPLES     (3U)
#define BOARD_INPUT_LONG_PRESS_MS        (1000U)
#define BOARD_INPUT_TASK_STACK           (3072U)
#define BOARD_INPUT_TASK_PRIORITY        (5U)
#define BOARD_INPUT_STOP_TIMEOUT_MS      (500U)

typedef struct board_key_state
{
    gpio_num_t gpio;
    bsp_key_t key;
    bool stable_active;
    bool candidate_active;
    uint8_t candidate_samples;
    bool long_press_sent;
    TickType_t pressed_at;
} board_key_state_t;

static board_key_state_t s_keys[] =
{
    {
        .gpio = BOARD_HOME_KEY_GPIO,
        .key = BSP_KEY_HOME,
    },
    {
        .gpio = BOARD_POWER_KEY_GPIO,
        .key = BSP_KEY_POWER,
    },
};

static TaskHandle_t s_input_task;
static SemaphoreHandle_t s_task_stopped;
static portMUX_TYPE s_handler_lock = portMUX_INITIALIZER_UNLOCKED;
static atomic_bool s_stop_requested;
static atomic_bool s_paused;
static bsp_input_cb_t s_input_cb;
static void *s_input_user_data;
static uint32_t s_callbacks_active;
static bool s_initialized;

static bool _board_input_key_active(gpio_num_t gpio)
{
    return gpio_get_level(gpio) == 0;
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

static void _board_input_sample(board_key_state_t *state, TickType_t now)
{
    const bool active = _board_input_key_active(state->gpio);
    if (active != state->candidate_active)
    {
        state->candidate_active = active;
        state->candidate_samples = 0;
        return;
    }
    if (state->candidate_samples < BOARD_INPUT_DEBOUNCE_SAMPLES)
    {
        state->candidate_samples++;
    }
    if (state->candidate_samples < BOARD_INPUT_DEBOUNCE_SAMPLES ||
            state->candidate_active == state->stable_active)
    {
        return;
    }

    state->stable_active = state->candidate_active;
    if (state->stable_active)
    {
        state->pressed_at = now;
        state->long_press_sent = false;
        _board_input_notify(state->key, BSP_KEY_EVENT_DOWN);
        return;
    }

    _board_input_notify(state->key, BSP_KEY_EVENT_UP);
    if (!state->long_press_sent)
    {
        _board_input_notify(state->key, BSP_KEY_EVENT_CLICK);
    }
}

static void _board_input_task(void *context)
{
    (void)context;
    const TickType_t long_press_ticks =
        pdMS_TO_TICKS(BOARD_INPUT_LONG_PRESS_MS);
    while (!atomic_load_explicit(&s_stop_requested, memory_order_acquire))
    {
        if (atomic_load_explicit(&s_paused, memory_order_acquire))
        {
            vTaskDelay(pdMS_TO_TICKS(BOARD_INPUT_POLL_MS));
            continue;
        }
        const TickType_t now = xTaskGetTickCount();
        for (size_t index = 0U;
                index < sizeof(s_keys) / sizeof(s_keys[0]); ++index)
        {
            board_key_state_t *state = &s_keys[index];
            _board_input_sample(state, now);
            if (state->stable_active && !state->long_press_sent &&
                    (now - state->pressed_at) >= long_press_ticks)
            {
                state->long_press_sent = true;
                _board_input_notify(state->key, BSP_KEY_EVENT_LONG_PRESS);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BOARD_INPUT_POLL_MS));
    }
    xSemaphoreGive(s_task_stopped);
    vTaskSuspend(NULL);
}

esp_err_t board_input_init(void)
{
    if (s_initialized)
    {
        return ESP_OK;
    }

    const gpio_config_t config =
    {
        .pin_bit_mask = (UINT64_C(1) << BOARD_HOME_KEY_GPIO) |
        (UINT64_C(1) << BOARD_POWER_KEY_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ONLY,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t result = gpio_config(&config);
    if (result != ESP_OK)
    {
        return result;
    }

    s_task_stopped = xSemaphoreCreateBinary();
    if (s_task_stopped == NULL)
    {
        result = ESP_ERR_NO_MEM;
        goto fail;
    }

    for (size_t index = 0U; index < sizeof(s_keys) / sizeof(s_keys[0]);
            ++index)
    {
        s_keys[index].stable_active = false;
        s_keys[index].candidate_active = false;
        s_keys[index].candidate_samples = 0;
        s_keys[index].long_press_sent = false;
        s_keys[index].pressed_at = 0;
    }
    atomic_store_explicit(&s_stop_requested, false, memory_order_release);
    atomic_store_explicit(&s_paused, false, memory_order_release);
    taskENTER_CRITICAL(&s_handler_lock);
    s_input_cb = NULL;
    s_input_user_data = NULL;
    s_callbacks_active = 0U;
    taskEXIT_CRITICAL(&s_handler_lock);

    if (xTaskCreatePinnedToCore(_board_input_task, "board_input",
                                BOARD_INPUT_TASK_STACK, NULL,
                                BOARD_INPUT_TASK_PRIORITY, &s_input_task,
                                CONFIG_MAIN_PROJECT_TASK_CORE_ID) != pdPASS)
    {
        result = ESP_ERR_NO_MEM;
        goto fail;
    }
    s_initialized = true;
    return ESP_OK;

fail:
    if (s_task_stopped != NULL)
    {
        vSemaphoreDelete(s_task_stopped);
        s_task_stopped = NULL;
    }
    s_input_task = NULL;
    return result;
}

esp_err_t board_input_deinit(void)
{
    if (!s_initialized)
    {
        return ESP_OK;
    }
    atomic_store_explicit(&s_stop_requested, true, memory_order_release);
    if (s_input_task != NULL)
    {
        if (xSemaphoreTake(s_task_stopped,
                           pdMS_TO_TICKS(BOARD_INPUT_STOP_TIMEOUT_MS)) != pdTRUE)
        {
            /* Leave the lifecycle intact so a later deinit attempt can
             * finish the handshake instead of orphaning the task. */
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelete(s_input_task);
        s_input_task = NULL;
    }
    s_initialized = false;
    if (s_task_stopped != NULL)
    {
        vSemaphoreDelete(s_task_stopped);
        s_task_stopped = NULL;
    }
    taskENTER_CRITICAL(&s_handler_lock);
    s_input_cb = NULL;
    s_input_user_data = NULL;
    s_callbacks_active = 0U;
    taskEXIT_CRITICAL(&s_handler_lock);
    return ESP_OK;
}

esp_err_t board_input_register_handler(bsp_input_cb_t callback,
                                       void *user_data)
{
    if (!s_initialized || callback == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ESP_ERR_INVALID_STATE;
    taskENTER_CRITICAL(&s_handler_lock);
    if (s_input_cb == NULL && s_callbacks_active == 0U)
    {
        s_input_cb = callback;
        s_input_user_data = user_data;
        result = ESP_OK;
    }
    taskEXIT_CRITICAL(&s_handler_lock);
    return result;
}

esp_err_t board_input_unregister_handler(void)
{
    if (!s_initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }
    const bool called_from_input_task =
        xTaskGetCurrentTaskHandle() == s_input_task;
    uint32_t callbacks_active = 0U;
    esp_err_t result = ESP_OK;

    taskENTER_CRITICAL(&s_handler_lock);
    callbacks_active = s_callbacks_active;
    if (called_from_input_task && callbacks_active != 0U)
    {
        result = ESP_ERR_INVALID_STATE;
    }
    else
    {
        s_input_cb = NULL;
        s_input_user_data = NULL;
    }
    taskEXIT_CRITICAL(&s_handler_lock);

    while (result == ESP_OK && callbacks_active != 0U)
    {
        vTaskDelay(1);
        taskENTER_CRITICAL(&s_handler_lock);
        callbacks_active = s_callbacks_active;
        taskEXIT_CRITICAL(&s_handler_lock);
    }
    return result;
}

esp_err_t board_input_prepare_sleep(uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!s_initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store_explicit(&s_paused, true, memory_order_release);
    return ESP_OK;
}

esp_err_t board_input_complete_sleep(uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!s_initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store_explicit(&s_paused, false, memory_order_release);
    return ESP_OK;
}

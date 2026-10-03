/**
 * @brief  Host test for the exclusive BSP display lease registry.
 * @note   Compiles the production src/bsp_hal.c against a board shim.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "bsp_hal.h"

static int s_panel;
static int s_panel_io;
static int s_touch;
static int s_touch_io;
static bool s_withhold_port;

static const bsp_display_port_t s_port =
{
    .width = 368,
    .height = 448,
    .panel = &s_panel,
    .panel_io = &s_panel_io,
    .touch = &s_touch,
    .touch_io = &s_touch_io,
    .transport =
    {
        .kind = BSP_DISPLAY_TRANSPORT_QSPI,
        .clock_hz = 40000000U,
        .max_transfer_lines = 10U,
        .dma_max_full_lines = 44U,
        .transaction_queue_depth = 2U,
        .data_lines = 4U,
        .bits_per_pixel = 16U,
        .psram_dma_direct = false,
    },
    .te = { .enabled = false, .gpio_num = -1, .intr_type = 0 },
};

esp_err_t board_init(void)
{
    if (s_withhold_port)
    {
        return ESP_OK;
    }
    return bsp_display_set_port(&s_port);
}

esp_err_t board_deinit(void)
{
    return ESP_OK;
}

bsp_capabilities_t board_get_capabilities(void)
{
    return BSP_CAPABILITY_DISPLAY | BSP_CAPABILITY_TOUCH |
           BSP_CAPABILITY_INPUT;
}

bsp_wakeup_descriptor_t board_get_wakeup_descriptor(void)
{
    const bsp_wakeup_descriptor_t descriptor = {0};
    return descriptor;
}

static void test_acquire_input_validation(void)
{
    bsp_display_lease_t lease = {0};
    assert(bsp_display_acquire("pre", &lease) == ESP_ERR_INVALID_STATE);
    assert(bsp_display_acquire("pre", NULL) == ESP_ERR_INVALID_ARG);
    assert(bsp_display_is_leased() == false);
}

static void test_exclusive_lease(void)
{
    assert(bsp_init() == ESP_OK);
    assert(bsp_display_is_leased() == false);

    bsp_display_lease_t first = {0};
    assert(bsp_display_acquire("first", &first) == ESP_OK);
    assert(first.id != 0U);
    assert(bsp_display_is_leased() == true);

    /* Exclusive: a second consumer is rejected while leased. */
    bsp_display_lease_t second = {0};
    assert(bsp_display_acquire("second", &second) == ESP_ERR_INVALID_STATE);
    assert(second.id == 0U);

    /* A stale identifier cannot release the current lease. */
    const bsp_display_lease_t stale = { .id = first.id + 100U };
    assert(bsp_display_release(&stale) == ESP_ERR_INVALID_ARG);
    assert(bsp_display_is_leased() == true);

    /* Deinit is refused while a lease is held. */
    assert(bsp_deinit() == ESP_ERR_INVALID_STATE);
    assert(bsp_is_initialized() == true);

    assert(bsp_display_release(&first) == ESP_OK);
    assert(bsp_display_is_leased() == false);

    /* Re-acquire after release advances the identifier. */
    bsp_display_lease_t again = {0};
    assert(bsp_display_acquire("again", &again) == ESP_OK);
    assert(again.id > first.id);
    assert(bsp_display_release(&again) == ESP_OK);

    assert(bsp_deinit() == ESP_OK);
    assert(bsp_is_initialized() == false);
}

static void test_release_and_deinit(void)
{
    assert(bsp_init() == ESP_OK);

    bsp_display_lease_t lease = {0};
    assert(bsp_display_acquire("owner", &lease) == ESP_OK);

    /* A stale identifier leaves the BSP up and the lease intact. */
    const bsp_display_lease_t stale = { .id = lease.id + 100U };
    assert(bsp_display_release_and_deinit(&stale) == ESP_ERR_INVALID_ARG);
    assert(bsp_is_initialized() == true);
    assert(bsp_display_is_leased() == true);

    /* The atomic release + teardown consumes the lease and closes the BSP. */
    assert(bsp_display_release_and_deinit(&lease) == ESP_OK);
    assert(bsp_is_initialized() == false);
    assert(bsp_display_is_leased() == false);

    /* Idempotent once already uninitialized. */
    assert(bsp_display_release_and_deinit(&lease) == ESP_OK);
}

static void test_ready_without_committed_port(void)
{
    s_withhold_port = true;
    assert(bsp_init() == ESP_OK);
    assert(bsp_is_initialized() == true);

    /* READY but no committed display port: acquire must be refused. */
    bsp_display_lease_t lease = {0};
    assert(bsp_display_acquire("noport", &lease) == ESP_ERR_INVALID_STATE);
    assert(bsp_display_is_leased() == false);

    assert(bsp_deinit() == ESP_OK);
    s_withhold_port = false;
}

int main(void)
{
    test_acquire_input_validation();
    test_exclusive_lease();
    test_release_and_deinit();
    test_ready_without_committed_port();
    printf("bsp display lease: ok\n");
    return 0;
}
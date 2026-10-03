#ifndef __BOARD_INIT_H__
#define __BOARD_INIT_H__

/*
 * Minimal board shim for the transactional HAL registry host test. It
 * exposes only the board hooks that src/bsp_hal.c calls, so the test can
 * exercise the production registry without the full board backend.
 */

#include <stdbool.h>

#include "bsp_hal.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t board_init(void);
esp_err_t board_deinit(void);
bsp_capabilities_t board_get_capabilities(void);
bsp_wakeup_descriptor_t board_get_wakeup_descriptor(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_INIT_H__ */
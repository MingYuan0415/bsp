#ifndef __BOARD_IMU_BRIDGE_H__
#define __BOARD_IMU_BRIDGE_H__

#include <stdint.h>

#include "bsp_hal.h"
#include "board_imu.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert one board IMU sample to the public BSP representation.
 *
 * @param raw is the board-driver sample to convert.
 * @param timestamp_us is the monotonic timestamp assigned by the BSP bridge.
 * @param sample receives the initialized public sample.
 */
void board_imu_bridge_sample(const board_imu_sample_t *raw,
                             int64_t timestamp_us,
                             bsp_imu_sample_t *sample);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_IMU_BRIDGE_H__ */

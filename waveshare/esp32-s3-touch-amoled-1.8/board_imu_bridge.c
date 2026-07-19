#include "board_imu_bridge.h"

#include <stddef.h>
#include <string.h>

void board_imu_bridge_sample(const board_imu_sample_t *raw,
                             int64_t timestamp_us,
                             bsp_imu_sample_t *sample)
{
    memset(sample, 0, sizeof(*sample));
    sample->timestamp_us = timestamp_us;
    sample->temperature_c = raw->temperature_c;
    sample->sensor_timestamp = raw->sensor_timestamp;
    sample->status_int = raw->status_int;
    sample->status0 = raw->status0;
    sample->status1 = raw->status1;
    sample->data_ready = raw->data_ready;
    for (size_t axis = 0; axis < 3U; ++axis)
    {
        sample->accel_mps2[axis] = raw->accel_g[axis] * 9.80665F;
        sample->gyro_dps[axis] = raw->gyro_dps[axis];
    }
}

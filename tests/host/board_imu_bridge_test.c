#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "board_imu_bridge.h"

static void _assert_close(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.0001F);
}

int main(void)
{
    const board_imu_sample_t raw =
    {
        .accel_g = {1.0F, -0.5F, 0.25F},
        .gyro_dps = {12.0F, -34.0F, 56.0F},
        .temperature_c = 26.75F,
        .sensor_timestamp = UINT32_C(0x123456),
        .status_int = 0xA5U,
        .status0 = 0x11U,
        .status1 = 0x22U,
        .data_ready = true,
    };
    bsp_imu_sample_t sample;
    memset(&sample, 0xA5, sizeof(sample));

    board_imu_bridge_sample(&raw, INT64_C(123456789), &sample);

    assert(sample.timestamp_us == INT64_C(123456789));
    _assert_close(sample.accel_mps2[0], 9.80665F);
    _assert_close(sample.accel_mps2[1], -4.903325F);
    _assert_close(sample.accel_mps2[2], 2.4516625F);
    _assert_close(sample.gyro_dps[0], 12.0F);
    _assert_close(sample.gyro_dps[1], -34.0F);
    _assert_close(sample.gyro_dps[2], 56.0F);
    _assert_close(sample.temperature_c, 26.75F);
    assert(sample.sensor_timestamp == UINT32_C(0x123456));
    assert(sample.status_int == 0xA5U);
    assert(sample.status0 == 0x11U);
    assert(sample.status1 == 0x22U);
    assert(sample.data_ready);
    assert(!sample.interrupt_active);
    assert(!sample.interrupt_level_valid);
    assert(sample.sequence == 0U);
    return 0;
}

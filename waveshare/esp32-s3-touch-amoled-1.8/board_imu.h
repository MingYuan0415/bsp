#ifndef __BOARD_IMU_H__
#define __BOARD_IMU_H__

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_io_expander.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief QMI8658C 7-bit I2C address used by this board. */
#define BOARD_IMU_I2C_ADDRESS (0x6BU)

/** @brief TCA9554 pin wired to QMI8658C INT1. */
#define BOARD_IMU_INT1_PIN (IO_EXPANDER_PIN_NUM_6)

/** @brief Default QMI8658C I2C transaction timeout. */
#define BOARD_IMU_I2C_TIMEOUT_MS (20)

/** @brief Highest accelerometer ODR accepted by the board driver. */
#define BOARD_IMU_SAMPLE_RATE_MAX_HZ (1000U)

/** @brief Opaque QMI8658C device instance. */
typedef struct board_imu board_imu_t;

/** @brief Accelerometer full-scale selection. */
typedef enum
{
    BOARD_IMU_ACCEL_RANGE_2G = 0,
    BOARD_IMU_ACCEL_RANGE_4G = 1,
    BOARD_IMU_ACCEL_RANGE_8G = 2,
    BOARD_IMU_ACCEL_RANGE_16G = 3,
} board_imu_accel_range_t;

/** @brief Gyroscope full-scale selection. */
typedef enum
{
    BOARD_IMU_GYRO_RANGE_16DPS = 0,
    BOARD_IMU_GYRO_RANGE_32DPS = 1,
    BOARD_IMU_GYRO_RANGE_64DPS = 2,
    BOARD_IMU_GYRO_RANGE_128DPS = 3,
    BOARD_IMU_GYRO_RANGE_256DPS = 4,
    BOARD_IMU_GYRO_RANGE_512DPS = 5,
    BOARD_IMU_GYRO_RANGE_1024DPS = 6,
} board_imu_gyro_range_t;

/** @brief Driver configuration. Zero-initialized values are replaced by defaults. */
typedef struct board_imu_config
{
    uint8_t i2c_address;                 /**< 7-bit address, normally 0x6b. */
    uint32_t i2c_clock_hz;               /**< Bus speed, normally 400 kHz. */
    uint32_t sample_rate_hz;             /**< Requested rate; nearest supported ODR. */
    board_imu_accel_range_t accel_range; /**< Accelerometer full scale. */
    board_imu_gyro_range_t gyro_range;   /**< Gyroscope full scale. */
    bool enable_interrupt;               /**< Route data-ready to INT1. */
} board_imu_config_t;

/** @brief One coherent QMI8658C sample. Units are g, degrees/second, and deg C. */
typedef struct board_imu_sample
{
    float accel_g[3];          /**< X/Y/Z acceleration in g. */
    float gyro_dps[3];         /**< X/Y/Z angular velocity in degrees/second. */
    float temperature_c;       /**< Die temperature in degrees Celsius. */
    uint32_t sensor_timestamp; /**< 24-bit sensor timestamp, expanded to 32 bits. */
    uint8_t status_int;        /**< STATUSINT register captured with the sample. */
    uint8_t status0;           /**< STATUS0 register captured with the sample. */
    uint8_t status1;           /**< STATUS1 register captured with the sample. */
    bool data_ready;           /**< True when either accelerometer or gyro is ready. */
} board_imu_sample_t;

/** @brief Callback invoked by board_imu_poll_interrupt() on an INT1 edge. */
typedef void (*board_imu_interrupt_cb_t)(bool active, void *user_data);

/**
 * @brief Fill a configuration with board-safe defaults.
 *
 * @param config receives the default values and must not be NULL.
 */
void board_imu_config_default(board_imu_config_t *config);

/**
 * @brief Create and initialize a QMI8658C on an existing I2C master bus.
 *
 * The I2C bus and optional TCA9554 handle remain owned by the caller. The
 * returned device owns only its I2C device handle and synchronization object.
 *
 * @param i2c_bus is the shared board I2C master bus.
 * @param io_expander is the TCA9554 base handle, or NULL to disable INT1
 *        polling. When supplied, INT1 is configured as an input.
 * @param config selects address, ODR, and sensor ranges; NULL uses defaults.
 * @param out_device receives the owned device instance.
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_imu_create(i2c_master_bus_handle_t i2c_bus,
                           esp_io_expander_handle_t io_expander,
                           const board_imu_config_t *config,
                           board_imu_t **out_device);

/**
 * @brief Destroy a QMI8658C instance and release its I2C device.
 *
 * @param device is the instance to release; NULL is accepted.
 * @return ESP_OK on success; otherwise the first cleanup error.
 */
esp_err_t board_imu_destroy(board_imu_t *device);

/**
 * @brief Report whether the device has a live I2C handle.
 *
 * @param device is the instance to inspect.
 * @return true when the instance can access the sensor.
 */
bool board_imu_is_available(const board_imu_t *device);

/**
 * @brief Configure the requested accelerometer and gyroscope output rate.
 *
 * The nearest supported QMI8658C ODR is selected independently for each
 * sensor. The device must be disabled while its clock registers are changed.
 *
 * @param device is the initialized sensor.
 * @param sample_rate_hz is in the range 1..BOARD_IMU_SAMPLE_RATE_MAX_HZ.
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_imu_set_sample_rate(board_imu_t *device,
                                    uint32_t sample_rate_hz);

/**
 * @brief Enable or disable accelerometer and gyroscope output.
 *
 * @param device is the initialized sensor.
 * @param enabled selects the output mode.
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_imu_set_enabled(board_imu_t *device, bool enabled);

/**
 * @brief Read one coherent accelerometer/gyroscope sample.
 *
 * @param device is the initialized sensor.
 * @param sample receives the converted sample and status registers.
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_imu_read(board_imu_t *device, board_imu_sample_t *sample);

/**
 * @brief Return the data-ready state captured by the most recent read.
 *
 * @param device is the initialized sensor.
 * @param ready receives true when STATUS0 reported accelerometer or gyro data.
 * @return ESP_OK when a sample has been read; ESP_ERR_INVALID_STATE before
 *         the first successful read.
 */
esp_err_t board_imu_get_last_data_ready(const board_imu_t *device,
                                        bool *ready);

/**
 * @brief Read the current INT1 level through the TCA9554.
 *
 * @param device is the initialized sensor.
 * @param active receives true for a high INT1 level.
 * @return ESP_OK on success; ESP_ERR_NOT_SUPPORTED when no expander was given.
 */
esp_err_t board_imu_get_interrupt_level(board_imu_t *device, bool *active);

/**
 * @brief Poll INT1 and optionally notify a callback when its level changes.
 *
 * This helper is intentionally task-context only. It does not claim that
 * EXIO6 is an ESP32 wake source and performs no ISR registration.
 *
 * @param device is the initialized sensor.
 * @param changed receives true when the level differs from the prior poll.
 * @return ESP_OK on a successful poll; otherwise an ESP-IDF error.
 */
esp_err_t board_imu_poll_interrupt(board_imu_t *device, bool *changed);

/**
 * @brief Register the optional task-context INT1 edge callback.
 *
 * @param device is the initialized sensor.
 * @param callback is called after a level transition, or NULL to clear it.
 * @param user_data is passed to callback.
 * @return ESP_OK on success; otherwise an ESP-IDF error.
 */
esp_err_t board_imu_set_interrupt_callback(board_imu_t *device,
        board_imu_interrupt_cb_t callback,
        void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_IMU_H__ */

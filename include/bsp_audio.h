#ifndef __BSP_AUDIO_H__
#define __BSP_AUDIO_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Return the board's default audio format. */
bsp_audio_config_t bsp_audio_get_default_config(void);

/**
 * @brief Initialize the board audio device around an existing I2C bus.
 *
 * The function creates the I2S full-duplex channels and the ES8311 control
 * path, but does not start DMA until bsp_audio_start() is called.
 *
 * @param i2c_bus is the board-owned I2C master bus.
 * @return ESP_OK on success, otherwise an ESP-IDF error.
 */
esp_err_t bsp_audio_init(void *i2c_bus);

/**
 * @brief Release all board audio resources.
 *
 * @return ESP_OK on success, otherwise the first cleanup error.
 */
esp_err_t bsp_audio_deinit(void);

/** @brief Report whether the codec and I2S resources are initialized. */
bool bsp_audio_is_available(void);

/** @brief Report whether both full-duplex channels are running. */
bool bsp_audio_is_started(void);

/**
 * @brief Change the PCM format while the device is stopped.
 *
 * The sample rate and MCLK ratio must match an ES8311 clock-table entry.
 * Supported sample rates are 8, 11.025, 12, 16, 22.05, 24, 32, 44.1, 48,
 * 64, 88.2, and 96 kHz. An MCLK ratio of zero selects 256x. For 24-bit
 * slots, the ratio must also be divisible by three so ESP-IDF does not
 * silently substitute a different I2S clock.
 *
 * @param config is copied by the BSP.
 * @return ESP_OK on success; ESP_ERR_INVALID_ARG for an unsupported format;
 *         ESP_ERR_INVALID_STATE while running.
 */
esp_err_t bsp_audio_configure(const bsp_audio_config_t *config);

/** @brief Start both TX and RX DMA channels and the ES8311 codec. */
esp_err_t bsp_audio_start(void);

/** @brief Stop both TX and RX DMA channels and mute the amplifier. */
esp_err_t bsp_audio_stop(void);

/**
 * @brief Write interleaved PCM samples to the speaker.
 *
 * @param data is the writable PCM buffer.
 * @param bytes is the number of bytes to transmit.
 * @param written receives the number of bytes accepted when non-NULL.
 * @param timeout_ms is the maximum DMA wait, or UINT32_MAX for forever.
 * @return ESP_OK on success, otherwise an ESP-IDF error.
 */
esp_err_t bsp_audio_write(void *data, size_t bytes, size_t *written,
                          uint32_t timeout_ms);

/**
 * @brief Read interleaved PCM samples from the microphone.
 *
 * @param data receives the PCM bytes.
 * @param bytes is the requested number of bytes.
 * @param read receives the number of bytes returned when non-NULL.
 * @param timeout_ms is the maximum DMA wait, or UINT32_MAX for forever.
 * @return ESP_OK on success, otherwise an ESP-IDF error.
 */
esp_err_t bsp_audio_read(void *data, size_t bytes, size_t *read,
                         uint32_t timeout_ms);

/** @brief Set the speaker volume in the range 0..100 percent. */
esp_err_t bsp_audio_set_volume(uint8_t percent);

/** @brief Copy the current speaker volume. */
esp_err_t bsp_audio_get_volume(uint8_t *percent);

/** @brief Mute or unmute the speaker output. */
esp_err_t bsp_audio_set_mute(bool muted);

/** @brief Copy the current speaker mute state. */
esp_err_t bsp_audio_get_mute(bool *muted);

/** @brief Enable or disable the NS4150B power amplifier. */
esp_err_t bsp_audio_set_pa(bool enabled);

/** @brief Copy the requested NS4150B power-amplifier state. */
esp_err_t bsp_audio_get_pa(bool *enabled);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_AUDIO_H__ */

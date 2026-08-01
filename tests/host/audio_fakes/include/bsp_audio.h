#ifndef __AUDIO_FAKE_BSP_AUDIO_H__
#define __AUDIO_FAKE_BSP_AUDIO_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct bsp_audio_config
{
    uint32_t sample_rate_hz;
    uint8_t bits_per_sample;
    uint8_t channels;
    uint16_t mclk_multiple;
} bsp_audio_config_t;

esp_err_t bsp_audio_init(void *i2c_bus);
esp_err_t bsp_audio_deinit(void);
bool bsp_audio_is_available(void);
bool bsp_audio_is_started(void);
esp_err_t bsp_audio_configure(const bsp_audio_config_t *config);
esp_err_t bsp_audio_start(void);
esp_err_t bsp_audio_stop(void);
esp_err_t bsp_audio_write(void *data, size_t bytes, size_t *written,
                          uint32_t timeout_ms);
esp_err_t bsp_audio_read(void *data, size_t bytes, size_t *read,
                         uint32_t timeout_ms);
esp_err_t bsp_audio_set_volume(uint8_t percent);
esp_err_t bsp_audio_get_volume(uint8_t *percent);
esp_err_t bsp_audio_set_mute(bool muted);
esp_err_t bsp_audio_get_mute(bool *muted);
esp_err_t bsp_audio_set_pa(bool enabled);
esp_err_t bsp_audio_get_pa(bool *enabled);

#endif /* __AUDIO_FAKE_BSP_AUDIO_H__ */

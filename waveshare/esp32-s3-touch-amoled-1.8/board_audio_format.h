#ifndef __BOARD_AUDIO_FORMAT_H__
#define __BOARD_AUDIO_FORMAT_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Return whether ES8311 and ESP-IDF I2S support this PCM format. */
bool board_audio_format_is_supported(uint32_t sample_rate_hz,
                                     uint8_t bits_per_sample,
                                     uint8_t channels,
                                     uint16_t mclk_multiple);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_AUDIO_FORMAT_H__ */

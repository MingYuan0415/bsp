#ifndef __BOARD_AUDIO_H__
#define __BOARD_AUDIO_H__

#include "bsp_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initialize the Waveshare ES8311/NS4150B audio path. */
esp_err_t board_audio_init(void *i2c_bus);

/** @brief Release the Waveshare audio path. */
esp_err_t board_audio_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_AUDIO_H__ */

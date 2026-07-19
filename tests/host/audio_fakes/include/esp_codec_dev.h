#ifndef __AUDIO_FAKE_CODEC_DEV_H__
#define __AUDIO_FAKE_CODEC_DEV_H__

#include <stdbool.h>
#include <stdint.h>

#define ESP_CODEC_DEV_OK 0
#define ESP_CODEC_DEV_TYPE_IN_OUT 3

typedef void *esp_codec_dev_handle_t;
typedef struct audio_codec_if_t audio_codec_if_t;
typedef struct audio_codec_data_if_t audio_codec_data_if_t;

typedef struct esp_codec_dev_sample_info
{
    uint8_t bits_per_sample;
    uint8_t channel;
    uint16_t channel_mask;
    uint32_t sample_rate;
    uint16_t mclk_multiple;
} esp_codec_dev_sample_info_t;

typedef struct esp_codec_dev_cfg
{
    int dev_type;
    const audio_codec_if_t *codec_if;
    const audio_codec_data_if_t *data_if;
} esp_codec_dev_cfg_t;

esp_codec_dev_handle_t esp_codec_dev_new(esp_codec_dev_cfg_t *config);
void esp_codec_dev_delete(esp_codec_dev_handle_t device);
int esp_codec_dev_open(esp_codec_dev_handle_t device,
                       esp_codec_dev_sample_info_t *format);
int esp_codec_dev_close(esp_codec_dev_handle_t device);
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t device, int volume);
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t device, bool muted);

#endif /* __AUDIO_FAKE_CODEC_DEV_H__ */

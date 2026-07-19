#include "board_audio_format.h"

#include <stddef.h>

#define BOARD_AUDIO_DEFAULT_MCLK_MULTIPLE (256U)

typedef struct board_audio_clock_pair
{
    uint32_t sample_rate_hz;
    uint16_t mclk_multiple;
} board_audio_clock_pair_t;

/* Intersection of esp_codec_dev 1.6.1 ES8311 coefficients and IDF I2S. */
static const board_audio_clock_pair_t s_supported_clocks[] =
{
    {8000U, 128U}, {8000U, 192U}, {8000U, 256U}, {8000U, 384U},
    {8000U, 512U}, {8000U, 768U}, {8000U, 1024U},
    {11025U, 128U}, {11025U, 256U}, {11025U, 512U}, {11025U, 1024U},
    {12000U, 128U}, {12000U, 256U}, {12000U, 512U}, {12000U, 1024U},
    {16000U, 128U}, {16000U, 192U}, {16000U, 256U}, {16000U, 384U},
    {16000U, 512U}, {16000U, 768U}, {16000U, 1024U}, {16000U, 1152U},
    {22050U, 128U}, {22050U, 256U}, {22050U, 512U},
    {24000U, 128U}, {24000U, 256U}, {24000U, 512U}, {24000U, 768U},
    {32000U, 128U}, {32000U, 192U}, {32000U, 256U}, {32000U, 384U},
    {32000U, 512U}, {32000U, 576U},
    {44100U, 128U}, {44100U, 256U},
    {48000U, 128U}, {48000U, 256U}, {48000U, 384U},
    {64000U, 128U}, {64000U, 192U}, {64000U, 256U},
    {88200U, 128U},
    {96000U, 128U}, {96000U, 192U}, {96000U, 256U},
};

bool board_audio_format_is_supported(uint32_t sample_rate_hz,
                                     uint8_t bits_per_sample,
                                     uint8_t channels,
                                     uint16_t mclk_multiple)
{
    if ((bits_per_sample != 16U && bits_per_sample != 24U &&
            bits_per_sample != 32U) || channels == 0U || channels > 2U)
    {
        return false;
    }

    const uint16_t effective_mclk = mclk_multiple == 0U ?
                                    BOARD_AUDIO_DEFAULT_MCLK_MULTIPLE :
                                    mclk_multiple;
    if (bits_per_sample == 24U && (effective_mclk % 3U) != 0U)
    {
        return false;
    }

    for (size_t index = 0;
            index < sizeof(s_supported_clocks) / sizeof(s_supported_clocks[0]);
            ++index)
    {
        if (s_supported_clocks[index].sample_rate_hz == sample_rate_hz &&
                s_supported_clocks[index].mclk_multiple == effective_mclk)
        {
            return true;
        }
    }
    return false;
}

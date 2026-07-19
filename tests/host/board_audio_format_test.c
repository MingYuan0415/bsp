#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "board_audio_format.h"

typedef struct expected_clock_pair
{
    uint32_t sample_rate_hz;
    uint16_t mclk_multiple;
} expected_clock_pair_t;

static const expected_clock_pair_t s_expected_pairs[] =
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

static bool _clock_expected(uint32_t sample_rate_hz, uint16_t mclk_multiple)
{
    for (size_t index = 0;
            index < sizeof(s_expected_pairs) / sizeof(s_expected_pairs[0]);
            ++index)
    {
        if (s_expected_pairs[index].sample_rate_hz == sample_rate_hz &&
                s_expected_pairs[index].mclk_multiple == mclk_multiple)
        {
            return true;
        }
    }
    return false;
}

int main(void)
{
    static const uint32_t rates[] =
    {
        8000U, 11025U, 12000U, 16000U, 22050U, 24000U,
        32000U, 44100U, 48000U, 64000U, 88200U, 96000U,
    };
    static const uint16_t multiples[] =
    {
        128U, 192U, 256U, 384U, 512U, 576U, 768U, 1024U, 1152U,
    };
    static const uint8_t widths[] = {16U, 24U, 32U};

    for (size_t rate = 0; rate < sizeof(rates) / sizeof(rates[0]); ++rate)
    {
        for (size_t multiple = 0;
                multiple < sizeof(multiples) / sizeof(multiples[0]);
                ++multiple)
        {
            for (size_t width = 0;
                    width < sizeof(widths) / sizeof(widths[0]); ++width)
            {
                const bool expected = _clock_expected(
                                          rates[rate], multiples[multiple]) &&
                                      (widths[width] != 24U ||
                                       (multiples[multiple] % 3U) == 0U);
                assert(board_audio_format_is_supported(
                           rates[rate], widths[width], 2U,
                           multiples[multiple]) == expected);
            }
        }
    }

    assert(board_audio_format_is_supported(16000U, 16U, 1U, 0U));
    assert(!board_audio_format_is_supported(88200U, 16U, 2U, 0U));
    assert(!board_audio_format_is_supported(44100U, 24U, 2U, 256U));
    assert(board_audio_format_is_supported(48000U, 24U, 2U, 384U));
    assert(!board_audio_format_is_supported(176400U, 16U, 2U, 128U));
    assert(!board_audio_format_is_supported(16000U, 20U, 2U, 384U));
    assert(!board_audio_format_is_supported(16000U, 16U, 0U, 384U));
    assert(!board_audio_format_is_supported(16000U, 16U, 3U, 384U));
    assert(!board_audio_format_is_supported(16000U, 16U, 2U, 320U));

    puts("board audio format regression passed");
    return 0;
}

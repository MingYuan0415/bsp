#ifndef __AUDIO_FAKE_FREERTOS_H__
#define __AUDIO_FAKE_FREERTOS_H__

#include <stdint.h>

typedef uint32_t TickType_t;
typedef int BaseType_t;

#define pdTRUE             1
#define portMAX_DELAY      UINT32_MAX
#define configTICK_RATE_HZ 1000U

#endif /* __AUDIO_FAKE_FREERTOS_H__ */

#ifndef __TCA9554_FAKE_FREERTOS_H__
#define __TCA9554_FAKE_FREERTOS_H__

#include <stdint.h>

typedef uint32_t TickType_t;
typedef int BaseType_t;

#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#endif /* __TCA9554_FAKE_FREERTOS_H__ */

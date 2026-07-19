#ifndef __FREERTOS_FREERTOS_H__
#define __FREERTOS_FREERTOS_H__

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#endif /* __FREERTOS_FREERTOS_H__ */

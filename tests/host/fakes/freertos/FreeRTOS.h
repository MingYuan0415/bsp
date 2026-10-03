#ifndef __FREERTOS_FREERTOS_H__
#define __FREERTOS_FREERTOS_H__

#include <stdint.h>

typedef uint32_t TickType_t;

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(mux) ((void)(mux))
#define taskEXIT_CRITICAL(mux) ((void)(mux))

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#endif /* __FREERTOS_FREERTOS_H__ */

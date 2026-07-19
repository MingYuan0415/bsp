#ifndef __RTC_FAKE_FREERTOS_H__
#define __RTC_FAKE_FREERTOS_H__

#include <stdint.h>

typedef int BaseType_t;
typedef uint32_t TickType_t;

#define pdTRUE       1
#define portMAX_DELAY UINT32_MAX

#endif /* __RTC_FAKE_FREERTOS_H__ */

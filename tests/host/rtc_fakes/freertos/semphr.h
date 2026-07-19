#ifndef __RTC_FAKE_SEMPHR_H__
#define __RTC_FAKE_SEMPHR_H__

#include "freertos/FreeRTOS.h"

typedef struct fake_rtc_semaphore *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);

#endif /* __RTC_FAKE_SEMPHR_H__ */

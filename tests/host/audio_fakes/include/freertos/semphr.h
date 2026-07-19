#ifndef __AUDIO_FAKE_SEMPHR_H__
#define __AUDIO_FAKE_SEMPHR_H__

#include "freertos/FreeRTOS.h"

typedef struct audio_fake_semaphore *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);

#endif /* __AUDIO_FAKE_SEMPHR_H__ */

#ifndef __FREERTOS_TASK_H__
#define __FREERTOS_TASK_H__

#include "freertos/FreeRTOS.h"

TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);

#endif /* __FREERTOS_TASK_H__ */

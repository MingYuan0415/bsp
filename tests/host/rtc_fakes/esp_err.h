#ifndef __RTC_FAKE_ESP_ERR_H__
#define __RTC_FAKE_ESP_ERR_H__

#include <stdint.h>

typedef int32_t esp_err_t;

#define ESP_OK                   ((esp_err_t)0)
#define ESP_FAIL                 ((esp_err_t)-1)
#define ESP_ERR_NO_MEM           ((esp_err_t)0x101)
#define ESP_ERR_INVALID_ARG      ((esp_err_t)0x102)
#define ESP_ERR_INVALID_STATE    ((esp_err_t)0x103)
#define ESP_ERR_INVALID_SIZE     ((esp_err_t)0x104)
#define ESP_ERR_NOT_SUPPORTED    ((esp_err_t)0x106)
#define ESP_ERR_INVALID_RESPONSE ((esp_err_t)0x108)

#endif /* __RTC_FAKE_ESP_ERR_H__ */

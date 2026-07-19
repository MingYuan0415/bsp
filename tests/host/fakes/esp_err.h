#ifndef __ESP_ERR_H__
#define __ESP_ERR_H__

#include <stdint.h>

/** @brief Host-test representation of an ESP-IDF error code. */
typedef int32_t esp_err_t;

#define ESP_OK                ((esp_err_t)0)
#define ESP_FAIL              ((esp_err_t)-1)
#define ESP_ERR_NO_MEM        ((esp_err_t)0x101)
#define ESP_ERR_INVALID_ARG   ((esp_err_t)0x102)
#define ESP_ERR_INVALID_STATE ((esp_err_t)0x103)
#define ESP_ERR_TIMEOUT       ((esp_err_t)0x107)
#define ESP_ERR_INVALID_RESPONSE ((esp_err_t)0x108)

#endif /* __ESP_ERR_H__ */

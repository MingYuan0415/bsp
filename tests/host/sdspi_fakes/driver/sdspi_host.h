#ifndef __SDSPI_FAKE_HOST_H__
#define __SDSPI_FAKE_HOST_H__

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"

typedef struct sdmmc_host
{
    int flags;
    int slot;
    int max_freq_khz;
} sdmmc_host_t;

typedef struct sdspi_device_config
{
    spi_host_device_t host_id;
    gpio_num_t gpio_cs;
    gpio_num_t gpio_cd;
    gpio_num_t gpio_wp;
    gpio_num_t gpio_int;
    bool gpio_wp_polarity;
    uint16_t duty_cycle_pos;
    int8_t wait_for_miso;
} sdspi_device_config_t;

#define SDSPI_DEFAULT_DMA SPI_DMA_CH_AUTO
#define SDSPI_SLOT_NO_CD  GPIO_NUM_NC
#define SDSPI_SLOT_NO_WP  GPIO_NUM_NC
#define SDSPI_SLOT_NO_INT GPIO_NUM_NC

#define SDSPI_HOST_DEFAULT() \
    { .flags = 1, .slot = SPI2_HOST, .max_freq_khz = 20000 }
#define SDSPI_DEVICE_CONFIG_DEFAULT() \
    { .host_id = SPI2_HOST, .gpio_cs = 13, .gpio_cd = GPIO_NUM_NC, \
      .gpio_wp = GPIO_NUM_NC, .gpio_int = GPIO_NUM_NC, \
      .gpio_wp_polarity = false, .duty_cycle_pos = 0, .wait_for_miso = 0 }

#endif /* __SDSPI_FAKE_HOST_H__ */

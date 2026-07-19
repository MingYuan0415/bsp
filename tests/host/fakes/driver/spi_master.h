#ifndef __DRIVER_SPI_MASTER_H__
#define __DRIVER_SPI_MASTER_H__

#include "esp_err.h"

typedef int spi_host_device_t;

typedef struct spi_bus_config
{
    int max_transfer_sz;
} spi_bus_config_t;

#define SPI2_HOST       2
#define SPI_DMA_CH_AUTO 0

esp_err_t spi_bus_initialize(spi_host_device_t host,
                             const spi_bus_config_t *bus_config,
                             int dma_channel);
esp_err_t spi_bus_free(spi_host_device_t host);

#endif /* __DRIVER_SPI_MASTER_H__ */
